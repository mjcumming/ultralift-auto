# UltraLift Auto — Web UI & Calibration Reference

What every entity on the device web page (and in Home Assistant) means, group by group, with example readings. This is the operator-facing companion to the engineering design in [`boat_lift_design.md`](boat_lift_design.md) (§ references point there).

The page is ordered top to bottom: **Control → Status → Configuration → Advanced Tuning → Diagnostics → Bench**. The rule for what lives where: *Control is what you press, Status is what you glance at, Configuration is what you set at commissioning, Advanced Tuning is set-and-forget thresholds, Diagnostics is for chasing a problem, Bench is hands-on wiring work only.*

---

## 1 · Control

**Lift Status** *(headline)* — one line answering "what is the lift doing right now." Everything below it acts on that. Possible readings, highest priority first:

| Reading | Meaning |
|---|---|
| `EMERGENCY — descending to Ready (level failure)` | Correction failed or list past Tilt Critical with the boat high: both valves open, blower off, riding to Ready (ADR-015). **Stop** seals immediately. |
| `EMERGENCY — descending to Lowered (level failure)` | Ready still had residual list (or list still growing): continuing the ganged vent to the bottom. |
| `EMERGENCY — lowered, vents open (level failure)` | Emergency descent arrived at Lowered; vents stay open, `Lift Problem` ON, mode buttons refused until **Stop** acknowledges (FAULT + seal). |
| `FAULT — stall_no_progress` (etc.) | Latched safe stop + reason. Press **Stop** to clear. |
| `BYPASS — valve open, blower off (manual override)` | Hands-off manual mode (§6.6). Dark button panel. |
| `Raising → Lift` / `Lowering → Ready` | Moving, with the destination named. |
| `MANUAL raising…` / `MANUAL lowering…` | Moving without position feedback (angle not trusted) — bounded by timers and Stop only. |
| `Lowered — vent open` | Resting at the bottom, vents deliberately left open (they stay open at the bottom, always). |
| `Manual valve` | A valve's real position disagrees with what's commanded — someone operated it by hand (or it's stuck). |
| `Angle sensor OFFLINE - manual control` / `Angle OUT OF RANGE - manual control` | Height feedback lost; only manual jogs and Stop work (§6.5). |
| `Not calibrated` | No target moves until Lift + Lowered are captured. |
| `Lifted` / `Ready` / `Guide` / `Lowered` / `Between ready/lifted` … | Resting position. `Guide` = floating, bunks still centering the slip (ADR-016); only after both Guide captures. |

**Lift · Ready · Guide · Stop · Lower** — dock buttons plus a web/HA **Guide** (no fifth dock LED). Press a position and the controller picks raise vs lower itself; a press mid-move retargets (last press wins). **Stop** always cancels motion and commands safe outputs; at Lowered it seals the vent until Lower is pressed again; at **Guide** it stays sealed. Stop also clears a FAULT and exits Bypass. Guide refuses until both sides are captured. Lift parks at the **boat ceiling** with a boat aboard (or unknown) and the **empty ceiling** when confirmed empty.

**Auto-Maintain Height** *(default ON)* — automatically tops up Lift (to the load-selected shutoff). Ready recovery requires a confirmed loaded boat and acts only from 3 % low through the 5 % safe floor; below that, inflation is inhibited. Up-only; never auto-lowers.

**Auto-Maintain Level** *(default ON)* — keeps the two sides even: throttles the side that's ahead during every move, and corrects a developing list while parked at Lift. OFF = valves ganged, decisions still logged as `LEVEL(shadow)`.

**Bypass Mode** — opens both valves, blower off, controller idle (web/HA or panel Diagnostics; the dock red button no longer enters Bypass). The lift vents and floats; all button LEDs go dark. Turn OFF (or short-press Stop) to return to normal.

**Lift Ready** *(cover, blind)* — Home Assistant raise/lower/stop for the everyday envelope. Raise sends Lift, lower sends Ready, Stop is Stop. Its position is Lift Height; above 95% it shows fully raised. It does not go to Lower (use the Lower button or `Lift Command` → Lower), and a partial position command is ignored.

---

## 2 · Status

- **Valve Positions** — the valves' *real* end-stop positions from their feedback contacts, e.g. `Starboard CLOSED · Port CLOSED`. `MOVING` = mid-travel; `FAULT` = both contacts on (contact fault). This is the single source of truth for valve state — trust it over any inference.
- **Level Status** — how level the lift is / what leveling is doing: `Level OK — Port 0.4% low`, `Leveling — holding Starboard back` (in-move throttle), `Leveling — feeding Port` (at-rest pulse), `Auto-level OFF — …`, or a ganged-fallback reason (`Port IMU offline — ganged`).
- **Lift Problem** *(binary)* — the one flag to alert on: fault, bypass, angle not trusted, or uncalibrated.
- **Lift In Operation** *(binary)* — ON while a move *somebody asked for* is running (any button, panel, web, or HA), OFF when it finishes. Keeper top-ups and the emergency descent don't light it — watch Lift Activity for those.
- **Air Loss Alert** *(binary)* — ON when air is leaving abnormally: sinking while sealed, parked sag rate too high, or too many keeper interventions in one visit (§15.5). The keepers keep correcting either way — this is the "you should know about this" flag. Reason in Diagnostics → Air Loss Detail.
- **Lift Height** — position as % of the calibrated span (Lowered = 0 %, Lift / boat-max cal = 100 %). Can read below 0 (settled past the Lowered cal) or above 100 (empty lift riding toward Empty Max).
- **Bunk Height** — estimated bunk elevation relative to the waterline, from arm geometry. The surveyed true-Lowered datum is −13 in (below water); positive values are above water. Display only.
- **Boat Present** — the fail-safe presence latch from raise-speed classification (§5.1). ON means *boat aboard or unknown*.
- **Boat Load State** *(Diagnostics)* — `Confirmed empty`, `Confirmed other boat`, `Confirmed Cobalt`, or `Unknown`. Ready recovery needs any confirmed boat. Lift uses empty vs any-boat to pick the shutoff (unknown = boat ceiling). Tunable via `Empty Raise Rate Min` and `Heavy Boat Raise Rate Max`. Resets to `Unknown` wherever the boat could have changed.
- **Water Temperature** — DS18B20 in the water. Scanned at boot only — if it shows unknown after a sensor swap, restart.
- **Maintain Observe** — the keepers' visit summary: `At Lift — 2 top-ups, 1 level fixes — sag −0.30%/h`, or `Not parked at a maintained position`. Counters reset when the lift arrives at a new maintained position.
- **Lift Activity / Lift Position** — short stable tokens (`Idle/Raising/Lowering/Leveling/Fault/Bypass/Emergency Descent` and `Guide/Lowered/Ready/Lifted/Between/Unknown`) for exact-match HA automations. They duplicate the human lines above on purpose — trigger on these, read the others. `Guide` wins over `Lowered` when both zones overlap.

---

## 3 · Configuration — calibration, explained

The lift measures **arm angle**, not height. Calibration teaches it what your dock's angles mean: you park the lift at each real position and press a capture button; the controller records the current angle. Height % is then drawn linearly between two of those captures — **Lowered = 0 %** and **Lift (max with boat) = 100 %** — Ready and Guide are their own angles, and Empty Max is the no-boat Lift ceiling (not a mode).

**The workflow (once, at commissioning — §16.8):**

1. Drive the lift all the way down (true bottom, settled) → press **Calibrate: set LOWERED**, then **Calibrate Port: set LOWERED**.
2. Optional Guide (ADR-016): Stop-seal where the boat floats but the bunks still center the hull in the slip → **Calibrate: set GUIDE** + **Calibrate Port: set GUIDE**. Sitting still, vents closed. Then the web **Guide** button (and `Lift Command` → Guide) will go there and seal.
3. Boat on the bunks, sealed, walkable → **Calibrate: set READY** + **Calibrate Port: set READY**.
4. Raise as high as the heavy boat will go, verified level → **Calibrate: set LIFT** + **Calibrate Port: set LIFT**.
5. (Boat OFF only) raise to the empty-tank ceiling, verified level → **Calibrate: set EMPTY MAX** + **Calibrate Port: set EMPTY MAX**.

Why the Port captures too: the frame racks slightly, so the port sensor gets its *own* captures at the same physical positions — leveling then compares the two sides in percent space, and the racking cancels out (§16.1).

- **Calibration Summary** — every zone edge the captures produce, on one line: `Lowered: >48.0° (cal 50.0°)  Guide: 48.5..55.5°  Ready: 42.2..46.2°  Lift: -5.4°  Empty: -23.9°`. Guide shows `—` until captured. If a zone looks wrong, this is where you see it.
- **Lift Target (%)** — how close Lift approaches the load-selected ceiling (default 97). Stop short of the captured max so a heavier day still arrives; 100 % will miss when the boat is heavier than the capture.
- **Zone Tolerance (°)** — the single "close enough" band used for every position zone: at-Ready means within ±this of the Ready angle, and so on. Wider = zones easier to hit but sloppier; default 2°.
- **Restart** — reboots the controller (state is safe: it always boots to HOLD; at the bottom the vent rule reopens the vents itself).

Nudging without re-running the lift: each capture is also an editable number under **Advanced Tuning** (`Cal Angle — …`). Use those to trim a setpoint a fraction of a degree or restore a clobbered value; use the capture buttons when the lift is actually parked at the position.

---

## 4 · Advanced Tuning (set-and-forget)

All live-editable and stored on the device — an OTA does *not* overwrite values you've set.

- **Blower Max Runtime (min)** — absolute blower cap, any mode. The hard backstop; must exceed a real full raise (measured 134 s loaded → default 4 min).
- **Lower Timeout (min)** — a descent that never confirms its target gives up and seals after this.
- **Angle Plausibility Margin (°) / Angle Freshness (s)** — the trust ladder (§9.4): how far outside the calibrated span, and how stale, the angle may be before auto moves stop.
- **Stall Grace / Stall Timeout / Stall Min Progress** — raising must make progress (0.2°) at least every Timeout after Grace, or FAULT. Tuned from field data: 20 s / 15 s.
- **Empty Raise Rate Min (% per s)** — early climb at/above this = empty (§5.1). Ships at 99 (= never empty) until tuned.
- **Heavy Boat Raise Rate Max (% per s)** — early climb at/below this = Cobalt/heavy. This install **0.60 %/s**; between this and Empty = other/light boat. Both boat bands use the boat Lift shutoff; only Confirmed empty uses Empty Max.
- **Maintain Sag Deadband / Persist / Min Interval / Settle Delay / Smoothing** — when a sag counts and how often a top-up may fire. Defaults make a Ready top-up eligible after roughly four minutes below the trigger.
- **Ready Recovery Floor** — maximum allowed drop below Ready for automatic inflation (default 5 %). It must exceed the sag deadband; crossing it inhibits recovery and requires an operator.
- **Air Alert Sealed Drop / Sag Rate / Visit Events** — the three Air Loss Alert triggers (§15.5).
- **Rest Level Trigger / Persist** — when a parked list earns a correction pulse.
- **Level Deadband / Hysteresis / Min Hold / Catch-up Timeout** — the in-move leveling throttle's engage/release behaviour.
- **Tilt Critical (deg)** — never-exceed live list and Ready residual (default **3°** ≈ HydroHoist’s 3 in side-to-side at Lift). Range **1–5°** (1° is a test trip; 5° was the old wall). Past this with the boat high → emergency descent, not “keep correcting.”
- **Cal Angle — …** sliders — the editable calibration numbers (see §3 above).
- **Lift Command** *(select)* — built for Home Assistant automations/scenes (`select Lift` on departure): picking a destination fires the same request as the matching button, interlocks included. Includes **Guide** (dock-prep height) and **Lower** (phone on the walk to the dock). Reads the destination during a commanded move, otherwise `—`. Everyday Lift/Ready from HA should use the **Lift Ready** cover. On this web page just use the buttons — it's filed down here to stay out of the way.

---

## 5 · Diagnostics

Read-only. **Arm Angle Starboard/Port** (raw degrees), **Height Port** (slave side's own %), **Level Error** (Port minus Starboard, %), **IMU OK** flags, **Lift Height (filtered)** (wave-stripped), **Lift Wave P-P** (60 s wave amplitude), **Lift Sag Rate** (%/h drift), **Visit Height Top-ups / Visit Level Events** (per-visit keeper counters, for HA graphs), **Air Loss Detail** (why the alert is on), **Last Stop Reason** (why the last move ended — first place to look when "the valve closed by itself"), **Last Move Duration**, **Uptime / WiFi Signal / Firmware Build**.

---

## 6 · Bench / Wiring Test

Hands-on only. **Bench Test (FSM off)** suspends the state machine so the relay/valve switches below it can be toggled by hand for wiring verification; button presses are ignored (but logged). The red dock button remains a universal kill even on the bench. **Turn Bench Test OFF for normal service.**

**Test: inject level fail** — one-shot. The level supervisor takes the same hard-stop path as a real list past Tilt Critical (then ADR-015 descent). Boots OFF; clears itself when the descent starts. **This moves the lift.** Bench Test must be OFF. A valve toggle here is *not* that test: with the FSM running, `apply_outputs` overwrites bench valve switches every tick.
