# UltraLift Auto — Web UI & Calibration Reference

What every entity on the device web page (and in Home Assistant) means, group by group, with example readings. This is the operator-facing companion to the engineering design in [`boat_lift_design.md`](boat_lift_design.md) (§ references point there).

The page is ordered top to bottom: **Control → Status → Configuration → Advanced Tuning → Diagnostics → Bench**. The rule for what lives where: *Control is what you press, Status is what you glance at, Configuration is what you set at commissioning, Advanced Tuning is set-and-forget thresholds, Diagnostics is for chasing a problem, Bench is hands-on wiring work only.*

---

## 1 · Control

**Lift Status** *(headline)* — one line answering "what is the lift doing right now." Everything below it acts on that. Possible readings, highest priority first:

| Reading | Meaning |
|---|---|
| `FAULT — stall_no_progress` (etc.) | Latched safe stop + reason. Press **Stop** to clear. |
| `BYPASS — valve open, blower off (manual override)` | Hands-off manual mode (§6.6). Dark button panel. |
| `Raising → Lift` / `Lowering → Ready` | Moving, with the destination named. |
| `MANUAL raising…` / `MANUAL lowering…` | Moving without position feedback (angle not trusted) — bounded by timers and Stop only. |
| `Lowered — vent open` | Resting at the bottom, vents deliberately left open (they stay open at the bottom, always). |
| `Manual valve` | A valve's real position disagrees with what's commanded — someone operated it by hand (or it's stuck). |
| `Angle sensor OFFLINE - manual control` / `Angle OUT OF RANGE - manual control` | Height feedback lost; only manual jogs and Stop work (§6.5). |
| `Not calibrated` | No target moves until Lift + Lowered are captured. |
| `Lifted` / `Ready` / `Lowered` / `Lifted (max)` / `Between ready/lifted` … | Resting position. |

**Lift · Ready · Stop · Lower** — the same intents as the dock buttons, in the same order as the physical panel. Press a position and the controller picks raise vs lower itself; a press mid-move retargets (last press wins); **Stop** cancels any move, clears a FAULT, and exits Bypass. **Lift Max** sits last: the roof-clearance position, only reachable when the lift is confident the boat is off (§5.1) — expect it to refuse or demote to Lift otherwise.

**Auto-Maintain Height** *(default ON)* — automatically tops the lift back up when it sags at Ready, Lift, or Lift Max. Up-only; never auto-lowers.

**Auto-Maintain Level** *(default ON)* — keeps the two sides even: throttles the side that's ahead during every move, and corrects a developing list while parked at Lift. OFF = valves ganged, decisions still logged as `LEVEL(shadow)`.

**Bypass Mode** — opens both valves, blower off, controller idle (same as holding the red dock button ~3 s). The lift vents and floats; all button LEDs go dark. Turn OFF (or short-press Stop) to return to normal.

---

## 2 · Status

- **Valve Positions** — the valves' *real* end-stop positions from their feedback contacts, e.g. `Starboard CLOSED · Port CLOSED`. `MOVING` = mid-travel; `FAULT` = both contacts on (contact fault). This is the single source of truth for valve state — trust it over any inference.
- **Level Status** — how level the lift is / what leveling is doing: `Level OK — Port 0.4% low`, `Leveling — holding Starboard back` (in-move throttle), `Leveling — feeding Port` (at-rest pulse), `Auto-level OFF — …`, or a ganged-fallback reason (`Port IMU offline — ganged`).
- **Lift Problem** *(binary)* — the one flag to alert on: fault, bypass, angle not trusted, or uncalibrated.
- **Air Loss Alert** *(binary)* — ON when air is leaving abnormally: sinking while sealed, parked sag rate too high, or too many keeper interventions in one visit (§15.5). The keepers keep correcting either way — this is the "you should know about this" flag. Reason in Diagnostics → Air Loss Detail.
- **Lift Height** — position as % of the calibrated span (Lowered = 0 %, Lift cal = 100 %). Can read below 0 (settled past the Lowered cal) or above 100 (Lift Max territory, or an empty lift riding high).
- **Bunk Height** — the same position converted to real inches of bunk rise above the Lowered cal, from arm geometry. Display only.
- **Boat Present** — the fail-safe presence latch from raise-speed classification (§5.1). ON means *boat aboard or unknown*; it blocks Lift Max. Resets to ON whenever the boat could have changed (at the bottom, or after bypass).
- **Water Temperature** — DS18B20 in the water. Scanned at boot only — if it shows unknown after a sensor swap, restart.
- **Maintain Observe** — the keepers' visit summary: `At Lift — 2 top-ups, 1 level fixes — sag −0.30%/h`, or `Not parked at a maintained position`. Counters reset when the lift arrives at a new maintained position.
- **Lift Activity / Lift Position** — short stable tokens (`Idle/Raising/Lowering/Leveling/Fault/Bypass` and `Lowered/Ready/Lifted/Lifted Max/Between/Unknown`) for exact-match HA automations. They duplicate the human lines above on purpose — trigger on these, read the others.

---

## 3 · Configuration — calibration, explained

The lift measures **arm angle**, not height. Calibration teaches it what your dock's angles mean: you park the lift at each real position and press a capture button; the controller records the current angle. Height % is then drawn linearly between two of those captures — **Lowered = 0 %** and **Lift = 100 %** — and Ready / Lift Max are remembered as their own angles on that same scale.

**The workflow (once, at commissioning — §16.8):**

1. Drive the lift all the way down (true bottom, settled) → press **Calibrate: set LOWERED**, then **Calibrate Port: set LOWERED**.
2. Float the boat level at the almost-down position → **Calibrate: set READY** + **Calibrate Port: set READY**.
3. Raise to the everyday stored height, verified level → **Calibrate: set LIFT** + **Calibrate Port: set LIFT**.
4. (Boat OFF only) raise to the winter/max height → **Calibrate: set LIFT MAX** (master only).

Why the Port captures too: the frame racks slightly, so the port sensor gets its *own* captures at the same physical positions — leveling then compares the two sides in percent space, and the racking cancels out (§16.1).

- **Calibration Summary** — every zone edge the captures produce, on one line: `Lowered: >48.0° (cal 50.0°)  Ready: 42.2..46.2°  Lift: 5.5°  Max: -18.0..-14.0°`. If a zone looks wrong, this is where you see it.
- **Lift Target (%)** — where "Lift" actually parks, as % of the span (default 98). This exists so the everyday position can sit safely *below* the Lift capture (e.g. roof clearance margin) without re-capturing.
- **Zone Tolerance (°)** — the single "close enough" band used for every position zone: at-Ready means within ±this of the Ready angle, and so on. Wider = zones easier to hit but sloppier; default 2°.
- **Restart** — reboots the controller (state is safe: it always boots to HOLD; at the bottom the vent rule reopens the vents itself).

Nudging without re-running the lift: each capture is also an editable number under **Advanced Tuning** (`Cal Angle — …`). Use those to trim a setpoint a fraction of a degree or restore a clobbered value; use the capture buttons when the lift is actually parked at the position.

---

## 4 · Advanced Tuning (set-and-forget)

All live-editable and stored on the device — an OTA does *not* overwrite values you've set.

- **Blower Max Runtime (min)** — absolute blower cap, any mode. The hard backstop; must exceed a real full raise (measured 134 s loaded → default 4 min).
- **Lower Timeout (min)** — a descent that never confirms its target gives up and seals after this.
- **Angle Plausibility Margin (°) / Angle Freshness (s)** — the trust ladder (§9.4): how far outside the calibrated span, and how stale, the angle may be before auto moves stop.
- **Stall Grace / Stall Timeout / Stall Min Progress** — raising must make progress (0.2°) at least every Timeout after Grace, or FAULT. Tuned from field data: 20 s / 30 s.
- **Empty Raise Rate Min (% per s)** — the boat-presence bar (§5.1): early climb at/above this = empty. Ships at 99 (= never empty) until you record an empty and a loaded raise and set it between them.
- **Maintain Sag Deadband / Persist / Min Interval / Settle Delay / Smoothing** — when a sag counts and how often a top-up may fire.
- **Air Alert Sealed Drop / Sag Rate / Visit Events** — the three Air Loss Alert triggers (§15.5).
- **Rest Level Trigger / Persist** — when a parked list earns a correction pulse.
- **Level Deadband / Hysteresis / Min Hold / Catch-up Timeout** — the in-move leveling throttle's engage/release behaviour.
- **Tilt Critical (deg)** — the level-divergence hard stop: sides apart by more than this → FAULT + seal (air can't fix a mechanical split).
- **Cal Angle — …** sliders — the editable calibration numbers (see §3 above).

---

## 5 · Diagnostics

Read-only. **Arm Angle Starboard/Port** (raw degrees), **Height Port** (slave side's own %), **Level Error** (Port minus Starboard, %), **IMU OK** flags, **Lift Height (filtered)** (wave-stripped), **Lift Wave P-P** (60 s wave amplitude), **Lift Sag Rate** (%/h drift), **Visit Height Top-ups / Visit Level Events** (per-visit keeper counters, for HA graphs), **Air Loss Detail** (why the alert is on), **Last Stop Reason** (why the last move ended — first place to look when "the valve closed by itself"), **Last Move Duration**, **Uptime / WiFi Signal / Firmware Build**.

---

## 6 · Bench / Wiring Test

Hands-on only. **Bench Test (FSM off)** suspends the state machine so the relay/valve switches below it can be toggled by hand for wiring verification; button presses are ignored (but logged). The red dock button remains a universal kill even on the bench. **Turn Bench Test OFF for normal service.**
