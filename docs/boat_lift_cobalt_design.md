# Cumming Floating Boathouse — Boat Lift Automation (Cobalt)
## ESPHome Control System Design — Revision F

**Project:** Automated control of a HydroHoist UltraLift UL2 8800 hydropneumatic boat lift — *Cobalt* boat
**Location:** Minaki, Ontario (floating boathouse, summer-only)
**Controller:** KinCony KC868-A16 (ESP32) running ESPHome — **live config `boat-lift-cobalt-v2.yaml`** (two-valve / two-IMU V2; `boat-lift-cobalt.yaml` is the single-valve V1 kept as a mirrored archive)
**Status:** **V2 installed and operating on the lift** since the 2026-07-18 cutover: split per-tank plumbing, valve B, and the second IMU are wired and field-verified; auto-leveling, the boat-presence classifier, and auto-maintain are all live and field-proven (2026-07-19 stress session). This revision brings the document back in sync with the deployed firmware (V2 rev G).

> **Changes from Rev E** *(documentation catch-up — the system moved fast in the field)*
> - **V2 is LIVE (§21):** cutover 2026-07-18. Per-tank plumbing with check-valved blower tee, valve B on Y3 (feedback DI3/DI4), IMU #2 on GPIO14. Device keeps the V1 name `boat-lift-cobalt` for HA/calibration continuity — identify firmware by the web ping comment / version string. ⚠ **Never flash the plain-V1 yaml onto the split plumbing** (valve B would never open → the port tank could fill but not vent).
> - **Auto-leveling proven live (§21):** first real throttle intervention 2026-07-19 (engaged at 2.0 % error, clean release). In-move Level Deadband retuned to **2 %** on the real ~45° span (≈0.9° of arm). The §21.6 at-rest keeper remains shadow-only.
> - **Boat-presence classifier tuned + operational (§13→§5.1):** empty-raise 1.51–1.73 %/s vs loaded 0.53 %/s; `Empty Raise Rate Min` set to **1.20 %/s**. Round-trip proven autonomously (empty verdict unblocks Lift Max; boat's return re-blocks it). "Decide en route" roof guard live.
> - **Auto-maintain field-proven (§17):** induced-sag tests recovered the lift from 33 % and 37 % plunges autonomously; overnight soak clean. The separate `Auto-Maintain Observe` switch is retired — the observer always runs.
> - **Blower chain now fail-safe (§3):** Y1 relay rewired to the NO path 2026-07-19 (coil de-energized = motor OFF, no cold-boot blip, dead controller = motor off) after the earlier fail-deadly topology was found the hard way.
> - **Physics discovery (§15.1):** an empty lift sealed mid-stroke is NOT stable below ~95–98 % — air compression makes the descent run away (twice measured: sealed at 92 → sank to 33 %; 96 → 37 %). Any future vent cutoff must trigger ≥98 %; a descent-rate/seal-failure alarm is the standing candidate, now calibratable from the captured profiles.
> - **Entity cleanup + automation-facing status (YAML rev G, 2026-07-20, §9):** new short-token **`Lift Activity`** (`Idle/Raising/Lowering/Fault/Bypass`) and **`Lift Position`** (`Lowered/Ready/Lifted/Lifted Max/Between/Unknown`) text sensors for exact-match HA automations; `Lift Status` keeps the human line minus the embedded percentage. Removed: Test LED switches, Bench Test Cal Load, Reset Fault (+`request_reset` — Stop is the universal clear), raw Pitch sensor, per-side valve position texts, `Lift State (raw)`. Bench inputs hidden from HA by default; hot display sensors calmed to 1 s.
> - **Bill of materials added (§3.2)** — the purchased hardware, for anyone reproducing the build.

> **Changes from Rev D** *(documentation catch-up + one behavior change)*
> - **Valve hardware corrected (§3, §7, §12):** the control valve is an **auto-return (spring/motor-return) valve on Y2 only** — energized = OPEN, de-energized = returns CLOSED. The Rev-D two-line (Y2 open / Y3 close) motorized-ball-valve model is retired; Y3 is spare (reserved for the V2 second valve, §21).
> - **Valve position feedback added (§7.1):** CR5-02 maintained aux contacts on **DI1 (fully CLOSED)** and **DI2 (fully OPEN)** report the valve's real position (OPEN / CLOSED / MOVING / FAULT-both-on). Currently instrumentation + a "Manual valve" disagreement annunciator; not yet an FSM input. *(Corrects Rev D's "DI1 is dead" note — DI1 works.)*
> - **Blower and valve start together (§6, §11):** the `MOVING_VALVE_OPENING` interlock state is **retired**. A raise energizes valve and blower simultaneously — the blower briefly dead-heading against a still-opening valve is acceptable, and the lift responds immediately. (Decision 2026-07-11.)
> - **`LOWERED_VENT` resting state (§6.1):** reaching Lowered leaves the vent open indefinitely so the lift keeps settling; Stop seals it. (YAML rev G.)
> - **Fourth setpoint — Lift Max (§5):** winter-storage / maximum-out-of-water position above the everyday Lift target; Lift itself is now a **percent target** (`Lift Target %`, default 98 %) rather than a captured zone.
> - **One tolerance (§6.2):** a single **Zone Tolerance (°)** defines every "at position" band (Lowered one-sided, Ready/Max ± band, Lift converted from the same knob). The separate Position Tolerance (%) is retired.
> - **Sensor sign confirmed (§2.2):** lift fully **lowered = positive** roll, raising drives the angle **negative**. Field calibration captured and good.
> - **Blower watchdog retired (§9.1, §11):** the absolute runtime cap always tripped first, so it is now the sole blower backstop.
> - **Phase 2 auto-maintain is implemented and live-capable (§17–§19):** real top-ups behind the default-off `Auto-Maintain` switch, with a max-top-ups-per-window lockout.
> - **RS485 panel link implemented (§22):** STA heartbeat + CMD parser at **9600 baud** to the dock touch panel; protocol in `boat_lift_link_protocol.md`.
> - **Design-for-V2 note (§21):** the one-valve system is being perfected first; changes should stay easy to port to the two-valve/two-IMU V2 (`boat-lift-cobalt-v2.yaml`).
> - **Docs consolidated (2026-07-11):** the separate auto-hold and V2-leveling design files were workspace, not records — their content now lives here (§17 auto-maintain as built, §21 V2 auto-level design) and the files are deleted. The lift design space is exactly two documents: **this one** and **`boat_lift_link_protocol.md`**.

> **Changes from Rev C**
> - **New failure model documented (§15):** wave-driven *asymmetric* air loss → progressive list (one tank vents, its back lifts clear of the water, water shifts forward, more air escapes). Root cause is mechanical/pneumatic; firmware's job is *maintain + detect + notify*, not cure.
> - **Scope split (§16):** with a single pneumatic zone (1V1M) the controller can hold **total height (auto-maintain)** but **cannot correct side-tilt** — true auto-level needs a pneumatic **zone split + second inclinometer** (§20, future hardware).
> - **Auto-maintain program (§17):** keep the lift at a setpoint against slow air loss. **Maintain-at-Ready is treated as safety-critical** — a sagging Ready could let the boat float free and drift away.
> - **Phased rollout (§18):** Observe → Maintain → Level. **Phase 1 (§19) is implemented now**: observe-only instrumentation (filtered height, wave peak-to-peak, sag rate, tilt proxy, *shadow* top-up counter) that drives **no outputs**.

> **Changes from Rev B**
> - **Sensor corrected:** the angle sensor is a **WitMotion WT901BC-TTL** (TTL, native WIT `0x55` protocol, parsed on **GPIO32**) — *not* the SINDT-485/Modbus device Rev B assumed. All RS485/Modbus content is retired.
> - **New control model:** three **named position setpoints** (Top / Ready / Lowered) reached by a **bidirectional "go-to-position"** behavior (the controller picks raise vs lower automatically).
> - **Button-LED behavior** specified (solid in-mode, flashing while transitioning, red Stop while moving).
> - **Health/status model** specified (component connectivity, equipment/ESP health, lift errors → Lift Status + Lift Problem).
> - **Absolute blower runtime cap** added (works in all modes, incl. manual bench test).

---

## 1. Purpose & Scope

Firmware/control design for automating the lift: move the boat between three positions with single-button "go there" operation, with local safety supervision, position sensing, dock buttons with status LEDs, and Home-Assistant visibility. Safety-critical behaviour stays entirely on the A16; network is observability only.

---

## 2. The Lift Mechanism (control-relevant facts)

(Source: HydroHoist UL2 Installation Manual.) Unchanged from Rev B:

- **Raises** by a **blower pushing air into the tanks** (positive pressure). Not a vacuum.
- **Lowering is passive:** blower off, valve open → air vents out under tank pressure → descends.
- **Hold** = valve sealed.
- 8800 unit is **1V1M** (one valve, one motor) — a single pneumatic zone.
- Blower load: **115 VAC, 11 A**, high inrush; OEM has GFCI + thermal + ~15-min auto-off.
- A manual PVC-tube exhaust procedure is the no-power manual backstop.
- **Control valve (as installed):** an **auto-return valve driven by one line (Y2)** — energized = drives OPEN, de-energized = returns CLOSED. Measured travel ≈ **7 s to open, ≈ 2.3 s to close**. It carries **CR5-02 maintained aux contacts** reporting fully-open / fully-closed (§7.1).

Position is sensed via **arm angle** (four-bar parallelogram, torsion-bar-tied to one degree of freedom → one inclinometer reads the whole lift). Angle → height by two-point calibration.

### 2.2 Sensor axis assignment (decided)

The WT901 reports two tilt axes; the lift uses them for two different jobs:

- **X-axis (Roll) = the height/position axis.** This is the arm-swing degree of freedom. `current_angle` tracks Roll, and **all height %, setpoints, calibration, and go-to logic derive from X** (see §5, §10). Resolves the Rev C open item "pick the arm axis."
  **Sign convention (field-confirmed, Rev E):** fully **lowered = positive** angle; raising swings the angle **negative** (calibrated span runs high-to-low). Height % handles the negative span transparently, and the one-sided Lowered zone ("at the lowered angle *or settled beyond it*", i.e. angle > `cal_lowered` − tolerance) depends on this sign — revisit it if the sensor is ever remounted.
- **Y-axis (Pitch) = the "sensor moved / something's wrong" axis — observe-only, likely to stay that way.** Pitch should *not* change as the lift swings; a drift in Y means the sensor has been knocked, rotated, or loosened (the Guard B idea, §9.4). As of Rev F the derived **`Lift Tilt`** sensor (pitch minus a captured reference) is the only pitch consumer — the raw pitch entity was removed in YAML rev G, and the V2 second IMU's cross-check (§21) is expected to supersede Guard B entirely.

### 2.1 Control truth table (steady-state)

| Mode | Blower | Valve |
|---|---|---|
| Raise | ON | OPEN |
| Hold | OFF | CLOSED |
| Lower | OFF | OPEN |

The valve is OPEN in both raise and lower; the **blower relay is the only difference**. The valve takes seconds to travel, but blower and valve are commanded **independently and simultaneously** (Rev E): a raise energizes both at once, and the blower briefly dead-heads while the valve finishes opening. No sequencing interlock.

---

## 3. Hardware Architecture (as built)

| Role | Part | Interface to A16 |
|---|---|---|
| Controller | KinCony **KC868-A16** (ESP32, esp-idf) | — |
| **Angle sensor (starboard / master)** | **WitMotion HWT901B-TTL** (TTL, native `0x55` protocol, 9600) | **GPIO32** (UART RX, read-only). VCC 12 V, GND shared. TTL is 3.3 V → direct. |
| **Angle sensor (port / slave, V2)** | second **HWT901B-TTL** | **GPIO14 / HT3** (UART0 — serial logging off, `baud_rate: 0`) |
| Water temp | **DS18B20** (1-Wire) | **GPIO33** + 4.7 kΩ pull-up to 3.3 V. *Scans at boot only — reboot to detect.* |
| Blower switch | RIB2401B pilot + NT90 12 V relay (drives 115 VAC blower). **NO path — fail-safe: coil de-energized = motor OFF** (rewired 2026-07-19). | **Output Y1** (drives relay coil) |
| Control valves | auto-return motorized ball valves (energized = open), manual override | **Y2 = starboard (valve A)**, **Y3 = port (valve B, V2)** |
| Valve feedback | CR5-02 maintained aux contacts (common = white → DI COM) | Valve A: **DI1 = CLOSED (yellow), DI2 = OPEN (green)**; valve B: **DI3 = CLOSED, DI4 = OPEN** |
| Buttons (×4) | 19 mm illuminated momentary, NO + LED ring | switch → DI, LED ring → output Y |
| Panel link | RS485 to dock touch panel (§22) | **GPIO13 TX / GPIO16 RX @ 9600** |
| Power | Mean Well 12 V DIN | 12 V rail |

*(No hardware top-limit switch is fitted — Rev D planned one on DI6; raising stops on the calibrated target + the absolute blower cap.)*

### 3.2 Bill of materials (as purchased)

Everything below is off-the-shelf (Amazon-grade); the only "special" part is the lift itself.

| Qty | Item | Role |
|---|---|---|
| 1 | **KinCony KC868-A16** — ESP32 relay/IO board (16 opto inputs, 16 outputs, RS485) | the controller |
| 2 | **WitMotion HWT901B-TTL** — 9-axis inclinometer, 0.05° XY accuracy, Kalman-filtered | arm angle, one per side |
| 2 | **Motorized ball valve, 1″ NPT, 304 SS, full port** — 2-wire auto-return (spring/motor return, normally closed), quick-release actuator, IP67, 12–24 V | per-tank vent/fill valves (CR5-02-style aux feedback contacts) |
| 1 pk (2) | **EPLZON NT90 power relay** — 12 VDC coil, 30/40 A SPDT (1NO 1NC), flange mount | blower motor switching (115 VAC, 11 A, high inrush) |
| 4 | **DMWD 19 mm momentary push button** — 12–24 V ring LED, 1NO 1NC, waterproof (white / orange / red / green) | dock control panel (Lift / Ready / Stop / Lower) |
| 1 | **Namunanee outdoor junction box** — 13″×9.2″×5.6″ hinged, internal mounting panel | controller enclosure |
| 1 | **Zulkit IP65 hinged ABS box** — 5.9″×3.9″×2.8″ w/ cable glands | remote-side electronics (IMU/valve wiring) |
| — | **1″ NPT 304 SS street elbows + 3-way tee** | valve plumbing / check-valved blower tee |
| — | **1-¼″ reinforced suction/discharge hose** | blower-to-tank air lines |
| 1 | **Cat6 outdoor direct-burial cable, 35 ft** | dock run (panel link / low-voltage signals) |
| 1 | **DS18B20** waterproof probe | water temperature |
| 1 | **Mean Well 12 V DIN supply** | logic + valve/LED power |

**A16 specifics:** I²C GPIO4/5; opto inputs PCF8574 @ **0x22** (DI1–8) / **0x21** (DI9–16); outputs PCF8574 @ **0x24** (Y1–8) / **0x25** (Y9–16). Inputs are dry-contact-to-GND, `inverted: true`. Outputs drive DC loads (relay coils, LED rings) off the output bank's 12 V input. *(Rev D called DI1 dead; it works and now carries the valve-closed contact. The genuinely bad channel found in the field was **DI15/X15**, which is why the Ready button moved to DI11.)*

### 3.1 Dock button wiring (DMWD 19 mm, confirmed)
- **Switch (NO) = YELLOW + WHITE** (open at rest, closes on press). Spare NC = GREEN (unused). **Use NO** — fail-safe (broken wire = not pressed).
- **LED ring = RED (+) / BLACK (−)**, ~12 V (built-in resistor).

---

## 4. Control Architecture: Supervised FSM

A small **go-to-position FSM** beneath an always-on **safety supervisor** that can force FAULT from any state. Three structural rules (unchanged from Rev B):

1. **Intents, not actions.** Buttons/HA emit requests; one validated path changes state.
2. **Outputs are a pure function of state** — blower/valve/LEDs set in one place (`apply_outputs`), re-asserted every tick.
3. **The supervisor preempts** — monitors force FAULT (or safe-hold) ahead of user intent.

`make_safe` = valve commanded closed, blower off; it backs both HOLD and FAULT.

---

## 5. Position Modes & Setpoints

Four **calibrated setpoints** (Rev E: Lift Max added), each a target the lift can be sent to:

| Mode | Button (LED) | Position | Setpoint |
|---|---|---|---|
| **Lift** | ⚪ White | Everyday stored position | **`Lift Target %`** (default 98 % of the calibrated span — a percent target, not a captured zone) |
| **Lift Max** | *(HA/web/panel only)* | Winter storage — maximum out of water, beyond Lift | `cal_max` (captured angle) |
| **Ready** | 🟠 Orange | Almost down — lowered, not yet floating | `cal_ready` (captured angle) |
| **Lowered** | 🟢 Green | All the way down (boat floating free) | `cal_lowered` (~0 %) |

Plus 🔴 **Stop** (no position). Height % is derived linearly between `cal_lift` (100 %) and `cal_lowered` (0 %); Ready and Lift Max are captured as their own angles. **Naming:** buttons/UI say *Lift / Ready / Lower*; the firmware's internal calibration ids remain `up` / `float` / `down` / `max` for flash-persistence compatibility.

The **current resting mode** is derived from the live angle vs each setpoint (within the Zone Tolerance band, §6.2): *Lifted (max) / Lifted / Ready / Lowered / Between*.

### 5.1 Boat-presence classifier + Lift Max roof guard (live, tuned)

**Why:** Lift Max (~148 % of the everyday span) drives the boat's tower into the boathouse roof — it must only ever run with the boat **off** the lift. There is no presence switch; presence is inferred from physics.

**How:** presence is classified on **raise cycles only**, from the early climb rate — a loaded raise is far slower than an empty one (measured: loaded **0.53 %/s**, empty **1.51–1.73 %/s**). The classifier times the first 15 %-points of climb and compares against the `Empty Raise Rate Min` slider, **set at 1.20 %/s** (biased high so a loaded raise can never fake EMPTY). Fail-safe polarity: unknown/unclassified = **boat ON**, and boat ON blocks `request_goto_max`. Presence resets to presumed-ON wherever the boat could change (LOWERED_VENT, bypass entry); the latch persists across reboots.

**Decide en route:** a Max press from a LOW start (<50 %) with presence unknown is allowed to launch — the classifier resolves on the way up, and ANY non-EMPTY outcome (boat-on verdict, throttle-corrupted window, failed arming) demotes the in-flight Max to the boat-safe Lift target. High starts stay hard-blocked without a positive EMPTY verdict from the previous raise. Leveling interaction: a slave-side throttle during the timing window inflates the master's apparent rate, so that direction **aborts** classification for the raise (the previous latch holds).

**Proven:** full round-trip observed autonomously — empty raise → EMPTY verdict → Max unblocked; boat's return raise → BOAT ON → Max re-blocked. A `Boat Present` occupancy sensor (Status) exposes the latch, and the display-only `Bunk Height` sensor (51 in arm: Δh = L·(sin θ_lowered − sin θ)) gives inches above the Lowered cal.

---

## 6. Go-to-Position State Machine

The defining new behaviour: pressing a mode button sends the lift to that setpoint, and the controller **chooses the direction automatically**.

### 6.1 States

| State | Blower | Valve | Meaning |
|---|---|---|---|
| **HOLD** | OFF | CLOSED | Resting (at a mode, or between). |
| **RAISING** | ON | OPEN | Pumping air in; rising toward target. Blower + valve energize **together** (Rev E — no valve-opening interlock state). |
| **LOWERING** | OFF | OPEN | Venting; descending toward target. |
| **LOWERED_VENT** | OFF | **OPEN** | Resting at Lowered with the vent left open — the lift keeps settling as far down as it physically goes, indefinitely. New commands accepted; **Stop seals the valve** (→ HOLD). |
| **FAULT** | OFF | CLOSED | Latched safe state + reason. |
| **BYPASS** | OFF | **OPEN** | Manual override (§6.6): valve open, blower off, FSM idle — lift vents/floats, hands-off. |

*(Rev D's `MOVING_VALVE_OPENING` state is retired; its numeric slot is kept reserved in the firmware so V1/V2 state numbering stays aligned.)*

### 6.2 Direction selection (on a mode-button request, from HOLD)

```
target = setpoint(mode)                 # Lift Target % / cal_max / cal_ready / cal_lowered
if  height < target - tol:  raise  ->  RAISING   (blower + valve ON together)
elif height > target + tol: lower  ->  LOWERING  (valve ON, blower off)
else:                       already there (no-op)
```

So **Ready (and Lift Max) are reachable from above or below**, and the blower only runs when actually raising:
- From **Lowered** → Ready: target is **above** → **raise** (blower ON, valve open).
- From **Lift** → Ready: target is **below** → **lower** (blower OFF, valve open — passive vent).
- → Lift / Lift Max: always a raise. → Lowered: always a lower.

Position checks use **one captured setpoint per mode + one configurable band**: **`Zone Tolerance (°)`** (Rev E — the single tolerance knob; the old Position Tolerance % is retired). The band defines both "already there" and "At <mode>":
- **Lowered:** one-sided — angle > `cal_lowered` − tol (at the lowered cal *or settled beyond it*; valid for the confirmed sensor sign, §2.2).
- **Ready / Lift Max:** captured angle ± tol.
- **Lift:** height ≥ `Lift Target %` − tol (tolerance converted °→% through the calibrated span).

### 6.3 Stopping

- **RAISING:** stop when the target zone is reached → HOLD. (No hardware top-limit; the backstops are the stall detector and the absolute blower cap.)
- **LOWERING:** stop when the target zone is reached → HOLD — **except a Lowered target**, which rests in **LOWERED_VENT** (vent stays open). The `Lower Timeout` timer backstops a descent that never confirms its zone (gives up and seals).
- **Stop button:** any moving state → HOLD; from LOWERED_VENT it seals the vent → HOLD.
- **Supervisor:** → FAULT (hard) or **auto-stop on loss of angle trust** (see §6.5).

### 6.4 Preconditions
A position-targeted move requires: **calibrated**, a **trusted angle** (§9.4), and **not faulted**.

### 6.5 Degraded / manual mode (angle not trusted)
When the angle is **offline** (no data) or **out of range** (implausible — §9.4), the controller drops to a **manual-recovery mode** rather than a latched fault:

- A **position-targeted (auto) move in progress is STOPPED** to HOLD (we can't trust where we are).
- **Red LED flashes** (critical); **Top → manual RAISE jog**, **Lowered → manual LOWER jog** (white/green), **Ready refused**, **Stop always live**.
- Manual jogs run **without position feedback**, bounded only by the **absolute blower cap** (up), **lower-timeout** (down), and **Stop**. They are *not* auto-stopped by loss of trust (you're deliberately recovering).
- Returning to a trusted angle restores normal go-to operation automatically.

This is the unified path for "the position sensor is wrong, so move it by hand."

### 6.6 Bypass mode (manual override)
A deliberate **hands-off override** that simply **opens the valve and ensures the blower is off**, then does nothing — the lift vents/floats and the FSM stays idle. Use it to service the lift or leave the pneumatics open without the controller acting.

- **Outputs:** valve **OPEN**, blower **OFF** (BYPASS state in `apply_outputs`). ⚠️ The lift is **vented** in bypass — it will not hold the boat on the pneumatics.
- **Entry:** **hold the red Stop button ~3 s** (`on_click min_length 3000 ms`), or toggle the **Bypass Mode** switch (web/HA) ON. Can be entered from any state (it overrides HOLD/FAULT/moving).
- **Exit:** **short-press the red Stop button** ("press again for real mode"), or toggle the switch OFF → returns to **HOLD**.
- **LEDs:** **all button LEDs OFF** in bypass — a dark panel means bypass.
- **Status:** Lift Status shows `BYPASS — valve open, blower off (manual override)`; **Lift Problem = ON** while bypassed (lift not in normal service).
- The absolute blower runtime cap (§11) is moot here (blower is off), and the angle-trust supervisor does not act in bypass.

---

## 7. Output Mapping

```
apply_outputs(state):                         # the ONLY writer of blower/valve
  RAISING:              valve OPEN,  blower ON       # energized together, no sequencing
  LOWERING:             valve OPEN,  blower OFF
  LOWERED_VENT:         valve OPEN,  blower OFF      # resting, vent left open
  BYPASS:               valve OPEN,  blower OFF      # manual override (§6.6)
  HOLD / FAULT:         valve CLOSED, blower OFF      # make_safe
  (suspended entirely while Bench Test Mode is ON)
```

### 7.1 Valve position feedback (CR5-02 aux contacts) — Rev E

Two maintained dry contacts report the valve's **real** end-stop position, independent of the open-loop travel timing: **DI2 = fully OPEN (green)**, **DI1 = fully CLOSED (yellow)**, common (white) → DI COM. Debounced 100 ms against seating chatter. Derived **Valve Position** text: `OPEN` / `CLOSED` / `MOVING` (mid-travel) / `FAULT` (both contacts on — should be impossible; treated as a sensor fault, not manual operation).

Uses today (**display/diagnostic only — not an FSM input yet**):
- Live valve state on the Diagnostics page, independent of what Y2 commands.
- **Manual-operation detector:** if command and feedback still disagree after a grace window (`Valve Travel Time` + 2 s) and the mismatch persists ~2 s, the status line shows **"Manual valve"** — someone operated the valve by hand (its manual override) while the controller commands it closed.

Future option (deliberately not done yet): use fully-open/fully-closed as FSM interlocks or fault triggers. With blower/valve independence (§2.1) there is no sequencing need; feedback-as-interlock would only add stuck-valve detection.

---

## 8. Button LED Scheme

Each position button's ring LED is driven by the controller (software, not hardwired):

| LED | SOLID when | FLASHING when | OFF otherwise |
|---|---|---|---|
| ⚪ Lift | resting **at Lift** | moving with **target = Lift** | |
| 🟠 Ready | resting **at Ready** | moving with **target = Ready** | |
| 🟢 Lowered | resting **at Lowered** | moving with **target = Lowered** | |
| 🔴 Stop | **moving (in operation)** | **critical** — fault OR angle not trusted (offline/out-of-range) | resting & healthy |

- Position LED **solid** = resting in that mode; **flashing** = heading to that mode.
- **Red solid** = "in operation — press to stop." **Red flashing** = "critical — something's wrong." Critical-flash outranks operation-solid, so a manual jog in a degraded state shows red flashing.
- In degraded/manual mode the **white (Lift) and green (Lowered)** LEDs are the live manual controls.
- **Bypass mode (§6.6): every LED OFF** — a dark panel is the bypass indicator (outranks all of the above).
- **Cadences (rev F):** position LEDs flash **slow** (~0.8 Hz, calm "working") while moving; **fast flash** (~2.5 Hz) is reserved for faults/critical on the red ring. Angle-sensor-**offline** gets its own red cadence: ~3 s fast-flash burst, then ~10 s dark, repeating — distinguishable from a latched fault's steady fast flash.

---

## 9. Health & Status Model

### 9.1 What we monitor
- **Components that can go offline** → connectivity flags:
  - **Angle sensor (MPU/WT901)** — ON = fresh valid `0x53` frames (freshness window ~3 s).
  - **Water-temp sensor (DS18B20)** — present/reading.
- **Equipment / ESP health** — uptime, WiFi signal, hardware.
- **Lift errors** — stall (raising, no progress), blower absolute-runtime cap, valve/position anomalies → FAULT with reason. *(Rev E: the separate per-raise blower watchdog is retired — the absolute cap always tripped first and is now the sole blower backstop.)*

### 9.2 Roll-up entities (MAIN view) — rev G status trio

Three text sensors carry the state, split by audience:

- **`Lift Activity`** *(machine-friendly — what the lift is DOING)*: exactly one of `Idle / Raising / Lowering / Fault / Bypass`. `LOWERED_VENT` reads as `Idle` (it is a resting state; the open vent shows on `Valve Positions`).
- **`Lift Position`** *(machine-friendly — WHERE it is)*: exactly one of `Lowered / Ready / Lifted / Lifted Max / Between / Unknown`. Valid mid-move (reports the zone being passed through); `Unknown` = angle untrusted or uncalibrated.
- **`Lift Status`** *(human-readable line, feeds the panel `msg=`)* via a priority ladder:
  1. `FAULT — <reason>`
  2. `BYPASS — valve open, blower off (manual override)`
  3. Moving → `MANUAL raising…` (untrusted) or `Raising -> Ready` (go-to; **no live % embedded** — rev G, so the string only changes on real transitions)
  4. `Lowered — vent open` (LOWERED_VENT)
  5. `Manual valve` (valve feedback disagrees with the closed command, §7.1)
  6. `Angle sensor OFFLINE — manual control` / `Angle OUT OF RANGE — manual control`
  7. `Not calibrated`
  8. Resting → `Lowered`, `Ready`, `Lifted`, `Lifted (max)`, `Between lowered/ready`, `Between ready/lifted`, or `Holding`

The short tokens exist for **exact-match Home Assistant automations** (`trigger: state, to: "Fault"`), and because a status string that embeds a live percentage floods the HA recorder with hundreds of distinct states per move.

- **Lift Problem** (binary, `problem`) — ON for fault / bypass / **angle not trusted** / auto-maintain lockout / uncalibrated → drives HA alerting.

### 9.3 UI tiers (web_server sorting groups)

The dock web UI files every entity into six groups: **1 Control** (mode buttons, Stop, Bypass) · **2 Status** (status trio, Valve Positions, heights, Boat Present, water temp) · **3 Configuration** (calibration captures + summary, everyday knobs, Restart) · **4 Advanced Tuning** (set-and-forget sliders — live-editable + flash-persisted, never hardcoded) · **5 Diagnostics** (read-only: arm angles, level status, last stop reason, uptime, WiFi, firmware build) · **6 Bench** (bench-test switch, relay toggles, raw button/valve-feedback contacts). Rev G: the bench binary inputs are additionally `disabled_by_default` in HA — visible at the dock, hidden from HA unless enabled per-entity.

### 9.4 Angle trust ladder
The angle feeds a three-rung trust model; only **trusted** allows automatic go-to moves:

| Rung | Test | Behaviour |
|---|---|---|
| **OFFLINE** | no fresh frames within `Angle Freshness` (~3 s) | manual recovery; `Angle Sensor OK` = off |
| **OUT OF RANGE** | data fresh, but live angle outside `[Top..Lowered] ± Angle Plausibility Margin` (default 10°) | manual recovery; auto-move stopped |
| **SENSOR MOVED** *(Guard B, Y-axis — placeholder)* | the **Y-axis (Pitch)** drifts > `Pitch Drift Limit` from its commissioning reference → sensor rotated/loose. Decoded now, **not yet acted on** (no installed reference to tune against). | manual recovery |
| **TRUSTED** | fresh + plausible + pitch stable | normal go-to operation |

Implemented as `angle_valid` (fresh + inside ±95°) → `angle_trusted` (valid + plausible). Loss of trust during a position-targeted move **auto-stops** to HOLD (§6.5). The V2 second IMU runs the same ladder independently (`angle2_valid` / `angle2_trusted`); leveling silently degrades to ganged-valve V1 behaviour when the slave loses trust.

> **Lesson logged (updated Rev E):** size the `angle_valid` gate to the *mounted* sensor, not the theoretical worst case. An early over-narrow gate falsely reported OFFLINE. With the mounting now field-confirmed (arm swing well inside ±90°, lowered = positive), the gate is **±95°** — beyond that the reading is nonsense. If the sensor is ever remounted, re-check both this gate and the one-sided Lowered zone (§6.2).

---

## 10. Calibration & Persistence

- **Four captures** (on the lift, via buttons): **Lift**, **Lift Max**, **Ready**, **Lowered** — each records the current angle. Positions are *not* exact: each is a setpoint plus the `Zone Tolerance` band (§6.2). **Done — captured in the field (2026-07); values confirmed good.**
- Each capture is also exposed as an **editable number** (Advanced Tuning) so a setpoint can be nudged — or fully restored — without re-running the lift. This earned its keep: after an accidental cal overwrite (2026-07-19) the whole calibration was restored remotely through the web API sliders. A **Calibration Summary** line shows every zone edge.
- Height % is linear between Lift and Lowered (negative span — lowered is the *high* angle, §2.2); Ready and Lift Max are their own angles. V2: the slave IMU has its **own** three captures at the same physical positions (frame racking is calibrated out in % space, §21.2).
- A **capture LEVEL ref** button records the reference pitch for the tilt proxy (§19). *(The old Bench Test Cal Load button was removed in rev G — the live device is calibrated, and one stray press had already clobbered the real captures; bench values live in git history.)*
- Stored in flash (`restore_value: yes`).
- **Guard:** target moves refused until calibrated; the plausibility window (§9.4) also derives from these captures.

---

## 11. Safety Contract (invariants)

- Blower ON **only** in RAISING and not faulted. **No valve-sequencing interlock** (Rev E): blower and valve energize together; a few seconds of dead-heading against the opening valve is accepted by design.
- HOLD/FAULT: valve commanded CLOSED, blower OFF. (LOWERED_VENT/BYPASS: valve OPEN, blower OFF.)
- **Absolute blower runtime cap** (`Blower Max Runtime`, all modes incl. manual test): the blower can never run longer than this, however it was energized. *Must exceed real full-raise time before live use (OEM auto-off ~15 min).*
- RAISING requires angle progress after a grace window, or FAULT (stall).
- **Automatic moves require a trusted angle** (§9.4); loss of trust mid-move **auto-stops** to HOLD and drops to manual recovery (§6.5). Manual jogs are bounded only by the hardware top-limit / lower-timeout / blower cap / Stop.
- **Power-up → safe** (valve closed, HOLD). **Power-loss → safe drift** (vents down to float). **Network loss → no change in safe behaviour.**
- **Bench Test Mode must be OFF for normal service** (it suspends FSM output control **and ignores button intents** — presses still log/flip the diagnostic sensors for wiring checks).

---

## 12. I/O Map (as built / planned)

| A16 terminal | Signal | Notes |
|---|---|---|
| GPIO32 | IMU #1 (starboard/master) TX | TTL `0x55` parse, read-only |
| GPIO14 (HT3) | IMU #2 (port/slave) TX | UART0 — serial logging off (`baud_rate: 0`); V2 |
| GPIO33 | DS18B20 water temp | + 4.7 kΩ pull-up |
| GPIO13 / GPIO16 | RS485 panel link TX / RX | 9600 8N1, §22 |
| Y1 | Blower relay coil | NT90 on the **NO path — fail-safe** (de-energized = motor OFF), `inverted: true` |
| Y2 | Valve A / starboard (auto-return: ON = open, OFF = closed) | CONFIRMED |
| Y3 | Valve B / port (V2) | CONFIRMED at the dock |
| Y4 / Y5 / Y6 / Y7 | Button LEDs: Lift(white) / Ready(orange) / Stop(red) / Lowered(green) | CONFIRMED |
| DI12 / DI13 / DI11 / DI16 | Buttons: Stop(red) / Lift(white) / Ready(orange) / Lowered(green) | CONFIRMED (Ready moved off dead DI15; DI14 unused) |
| DI1 / DI2 | Valve A feedback: fully CLOSED (yellow) / fully OPEN (green) | CR5-02 aux contacts, §7.1 |
| DI3 / DI4 | Valve B feedback: fully CLOSED / fully OPEN | same pattern, V2 |

*(No top-limit switch is fitted — the Rev-D DI6 plan was dropped.)*

---

## 13. Open Items

Done and retired to history: setpoint captures, sensor sign, valve polarity/pin map, move-time baselines (loaded raise 134 s / empty 17.8 s / full lower ~119 s, CSVs in `field-data/`), boat-presence classifier (§5.1 — tuned at 1.20 %/s and round-trip proven), Phase-1 data collection (auto-maintain field-proven 2026-07-19).

Still open:

- **Descent-rate / seal-failure alarm (top candidate).** Two triggers: height falling **while both valves are commanded closed** (seal failure / air loss — immediate notify; a boat-drift precursor at Ready, and exactly the §15.1 compression-spiral signature), and rate far above the known ~7 %/s profile during a commanded lower (mechanical failure). Cheap in the 250 ms supervisor, and the 2026-07-19 plunge captures provide the calibration data.
- **Implement the §21.6 at-rest level keeper** — rule + guard rails decided; still shadow-only pending boat-on/off rest-period validation of the `REST-LEVEL(shadow)` logs.
- **Tighten the stall thresholds from the recorded baselines** (still at the permissive grace 60 s / timeout 120 s / 0.2° shipped values; the 5-min blower cap remains the hard backstop).
- **Diagnose the command-to-valve-motion delay** (~1–2 s press-to-sound reported; firmware path ≈100 ms — use the §7.1 feedback timestamps to split firmware vs valve-motor lag).
- Decide manual jog: **latched-with-Stop** (current) vs **hold-to-run / deadman**.
- **Decide maintain-at-Ready policy** (float-away guard, §17): thresholds + whether a sustained Ready sag should escalate to notify/FAULT.
- Decide whether valve feedback should ever gate the FSM (stuck-valve fault) or stay display-only (§7.1).
- Retire the Lift Tilt pitch proxy entirely once the two-IMU level error is confirmed to cover the "sensor moved" case (§2.2, §9.4).

## 14. Build status (as of Rev F)

**V2 installed and operating on the lift** since the 2026-07-18 cutover: split per-tank plumbing, both valves with CR5-02 feedback, both IMUs, blower on the fail-safe NO path, dock buttons, RS485 panel link — all wired and field-verified. Firmware (V2 rev G): go-to-position FSM (four setpoints incl. Lift Max with the decide-en-route roof guard), LOWERED_VENT with the vent-always-open-at-bottom rule, auto-leveling (in-move throttle proven live; §21.6 rest keeper in shadow), boat-presence classifier (tuned, round-trip proven), auto-maintain (field-proven on induced sags, lockout guarded), angle trust ladder ×2, bypass mode, bench-test mode, absolute blower cap, "Manual valve" detector, and the rev-G status trio (`Lift Activity` / `Lift Position` / `Lift Status`).

Milestone trail: Rev C FSM/trust/bypass core (bench) → Rev D Phase-1 observe → Rev E install + calibration + valve feedback + auto-maintain → **2026-07-18 V2 cutover** → 2026-07-19 blower fail-safe rewire + classifier tuning + leveling/maintain field proofs → **2026-07-20 rev G declutter + automation status**.

---

## 15. Failure mode: wave-driven asymmetric air loss (observed)

Field report (boat not yet wired to the controller): the lift was found **noticeably listed to one side**, with overall height **drifted down**. Working hypothesis for the mechanism:

1. Wave action works air out of **one tank** faster than the other (a seal/check imperfection — the tanks are *not* a perfectly balanced pair).
2. That corner sinks → the **back of that tank lifts clear of the water**.
3. With less of the tank submerged, water inside runs forward and the buoyancy shifts → **more air escapes**, so the list **progresses** rather than self-correcting (positive feedback).

Consequences split cleanly:
- **Total height sag** (whole-system air loss) → *correctable by topping up* → **auto-maintain** (§17).
- **Side-to-side list** (differential air loss) → *not correctable by adding air to a single shared zone* (§16) → needs **manual mechanical adjustment** of the lift and/or a hardware zone split (§20).

Firmware's role for the list is **detect + notify**, and to make sure auto-maintain never *masks* a developing list by silently pumping (the tilt monitor annotates/gates maintenance).

### 15.1 The compression spiral (measured 2026-07-19)

A sealed lift mid-stroke is **not stable** when empty. Twice measured during induced-sag testing: sealed at 92 % → sank to 33 %; sealed at 96 % → 37 %. Mechanism: as the lift settles, tank air compresses under the growing water column, buoyancy falls, the lift settles further — a runaway that only re-balances around ~33 %. Consequences:

- The empty lift has **no stable sealed states between roughly 98 % and 35 %** — "seal it partway down" is not a thing.
- Any future automatic vent cutoff must trigger at **≥98 %** (valve close travel is ~2–3 s; below that the spiral wins).
- The captured plunge profiles calibrate the §13 descent-rate/seal-failure alarm.
- On the plus side: a single-tank leak on the *empty* lift **self-limits** at ~1.1 % differential — frame torsional stiffness converts it to symmetric sag, so the height-based auto-maintain (§17) is the real leak responder, not the level keeper.

## 16. Scope: maintain now, level later

| Capability | Achievable with current hardware? | Why |
|---|---|---|
| **Auto-maintain height** (hold a setpoint vs. slow air loss) | **Yes** | One blower + one valve controls total buoyancy. A top-up is just a normal go-to raise. |
| **Auto-level** (actively correct a side list) | **No — needs hardware** | 1V1M = a single pneumatic zone. Air enters at the common manifold pressure; you cannot pressurise only the low side. |
| **Detect a list + notify** | **Partly now, fully later** | The arm-mounted WT901 sees side-tilt only weakly (Pitch proxy). Reliable list sensing wants a **cradle-mounted second inclinometer** (§20). |

## 17. Auto-Maintain (height-keeper) — program intent

Hold the lift at a maintained setpoint against slow air loss by synthesising a normal go-to raise when a **persistent** sag is confirmed. It is a thin policy on top of the existing FSM (no new output path), so it inherits every backstop: blower runtime cap, stall detector, angle-trust auto-stop. It is **not** an autopilot, leveling system, or unattended-recovery system — the safe default is *observe, alert, and stop actuating* when the lift starts needing unusual correction.

> **Status (Rev F): implemented AND field-proven.** Phase 2 actuation is live behind the **`Auto-Maintain`** switch. Induced-sag tests (2026-07-19, supervised): the lift autonomously detected and recovered deep plunges (33 % and 37 % → back to ~105 % in <12 s of blower), the classifier re-graded the raise, leveling rode along, and the lockout window behaved. With the blower chain now fail-safe (NO path), unattended operation is no longer wiring-blocked.

**Maintained setpoints:** **Lift** (boat stored) and **Ready** (lowered, not yet floating). **Lowered is never maintained** (boat floating — no blower on a floating hull).

**Why Ready is safety-critical (the reason it gets its own keeper):** if the boat is parked at **Ready** and the lift slowly leaks/sags, the hull can **settle into the water, float free of the cradle, and drift away**. Holding Ready is therefore not convenience — it prevents a runaway boat. Ready sits partly in the wave zone, so it needs a **looser deadband / longer persistence** than Top.

**Arming rules (Phase 2):** arm only in **HOLD**, **calibrated**, **angle trusted**, **at a maintained setpoint**, after a **settle delay**. Never in FAULT/BYPASS/manual. Disarm on any commanded move or loss of trust. **Up-only** — maintain tops up, never vents to trim (passive venting is imprecise and would thrash the motor).

**Guards specific to maintain (Phase 2):**
- **Motor short-cycle protection** — minimum off-time between top-ups (blower is 115 VAC / 11 A, high inrush).
- **Can't-hold / valve-won't-seal guard** — if height sags again immediately after a top-up, repeatedly within a short window, escalate to **FAULT + notify** instead of pumping forever (catches a fast leak or a vent valve that won't seal).
- **Leak-rate alarm** — count top-ups over a rolling 24 h; **> N/24 h ⇒ notify**. A *shrinking* interval between top-ups is the worsening-leak tell.
- **Unattended-blower disclosure** — auto-maintain lets the blower energise with nobody present. It is gated behind an explicit, persisted enable and is **always logged + notified** (no silent 2 a.m. blower). Explicit amendment to the §11 safety contract.

### 17.1 As built — observer always on, actuation switched

- **The observer always runs** (no enable switch — retired in the rev-E declutter): filters height, estimates sag rate and wave movement, counts *shadow* top-ups that would have fired. Its outputs are the §19 diagnostic sensors.
- **`Auto-Maintain`** (switch, default OFF): the same sag detector performs **real top-ups through the normal `do_goto()` path**. Arms only while idle at Lift or Ready, never at Lowered. The detector: waits for the settle delay, uses the filtered height (won't chase waves), requires sag beyond the deadband to persist for the configured time, enforces a minimum interval between top-ups, and counts both shadow and real top-ups. Tuned values in service: sag deadband **3 %**, persist 60 s, min interval 12 min.

### 17.2 Lockout guardrail

`Maintain Alert Window (h)` + `Maintain Max Top-ups` cap how many real corrections one window allows. At the threshold, **`Auto-Maintain Lockout`** turns on, **`Lift Problem`** turns on, and further automatic top-ups are blocked. Lockout is **manual-reset only** (`Reset Auto-Maintain Lockout`). Intentional: a lift that needs frequent correction should page a human, not cycle the blower indefinitely.

### 17.3 Notification policy

- **Notify immediately:** Lift Problem on; Auto-Maintain Lockout on; latched fault; angle sensor offline/out of range; blower-cap or stall fault; (later, once proven) critical tilt.
- **Notify as warning:** unusually negative sag rate; frequent shadow top-ups while actuation is off; tilt warning active; rising top-up frequency short of lockout.
- **Never auto-correct:** tilt/list (until V2, §21); unknown position; untrusted angle; lowered/floating state; repeated sag past the lockout threshold.

### 17.4 Baselines to record at commissioning

Build a small table from real operation (the `Last Move Duration` sensor + `move START/END` log lines capture the move timings automatically):

- lift time **with boat** / **without boat**;
- lower time **with boat** / **without boat**;
- normal top-up duration, and top-up frequency at Lift and at Ready;
- normal idle sag rate, wave peak-to-peak, and pitch/Y tilt range.

These baselines later drive anomaly alerts: lift took much longer than normal (or finished suspiciously fast — no load or bad sensing), lowering overlong, top-ups past the expected daily count, tilt outside the learned range, idle height changing faster than expected. **None recorded yet — see §13.**

## 18. Phased rollout

1. **Phase 1 — Observe (shipped Rev D, §19):** instrumentation only, **drives no outputs**. Measures wave amplitude, sag rate, and a *shadow* top-up rate under candidate thresholds so the real thresholds come from data, not guesses.
2. **Phase 2 — Auto-maintain (implemented Rev E, default OFF):** the top-up policy is in the firmware behind the `Auto-Maintain` switch with a lockout guard (§17.1–§17.2); enable once Phase-1 data confirms the deadband / persistence / interval.
3. **Phase 3 — Auto-level:** designed and code-complete as **V2** (§21) — awaiting the zone-split hardware.

## 19. Observe instrumentation (as built, always on)

All entities are **diagnostic**, computed every 250 ms, and **never write blower/valve**. Always running (the old enable switch is retired).

**Data product (sensors):**
- **Lift Height (filtered)** — wave-stripped EMA (time constant = *Maintain Smoothing*). Separates drift from chop.
- **Lift Wave P-P (60 s)** — peak-to-peak of raw height over a 60 s window = live **wave amplitude** (sizes the deadband; confirms persistence rejects waves).
- **Lift Sag Rate (%/h)** — slope of the filtered height (negative = sagging) = **leak severity**.
- **Lift Tilt (°)** — Pitch minus a captured **level reference**; the legacy list proxy, superseded in practice by the V2 two-IMU **Level Error** and slated for retirement (§13).
- **Maintain Observe** (text) — armed where (Lift/Ready/idle), actuation on/off, real + shadow top-up counts, sag rate — the one-line summary (the earlier separate counter/indicator entities were folded into it during the declutter revs).

**Tunable thresholds (live `number`s, Advanced Tuning):** Maintain Sag Deadband (%), Maintain Persist (s), Maintain Min Interval (min), Maintain Settle Delay (s), Maintain Smoothing (s), Tilt Critical (°). In-service values **3 % / 60 s / 12 min / 180 s / 20 s / 5°** (sag deadband confirmed from field data 2026-07-19; the separate Tilt Warn tier was retired).

**The shadow detector** mirrors Phase-2 arming exactly (latched at Lift or Ready while in HOLD with a trusted angle, after the settle delay), counts a "would top up" when the *filtered* height stays below `setpoint − deadband` for *persist* seconds with the min-interval gap satisfied, then re-arms as if a top-up had restored the setpoint. With actuation ON the same event fires the real top-up.

## 20. Future hardware: zone split + second inclinometer (auto-level)

True list correction needs the controller to pressurise the **low side independently**:
- **Pneumatic zone split** — a **second motorised valve** so each tank bank fills/vents on its own line; the single blower is **time-shared** via a diverter/manifold (feed the low side).
- **Second inclinometer** — a **cradle-mounted** dual-axis sensor reading true cradle roll/pitch (the arm-mounted WT901 can't see cradle list reliably). It also cross-checks the first unit for the "sensor moved" guard (Guard B).
- **Control** — a slow outer **level loop** (bias which side gets air to drive roll toward zero) wrapped around the inner height-maintain loop, with hard limits (small per-correction air doses, max list before FAULT, never over-pump a developing list).

This is an **add-on**, explicitly out of scope until the leak is characterised and the mechanical fix (manual adjustment) is attempted first.

## 21. V2 — two-valve / two-IMU auto-leveling (LIVE)

Design agreed 2026-07-10; **cut over to the dock 2026-07-18** — split plumbing, valve B, and IMU #2 installed and field-verified, with the device keeping the V1 name `boat-lift-cobalt` for HA/calibration continuity (identify firmware via the web ping comment; ⚠ the plain-V1 yaml must never be flashed onto the split plumbing — valve B would never open). First live throttle intervention proven 2026-07-19 (engaged at 2.0 % error, clean release; a full boat-on descent ran within ±2 % with zero interventions).

As-built highlights: the **§21.3 hard stop** (`level_divergence` FAULT when the filtered two-IMU difference, in degrees through the master span, exceeds `Tilt Critical (°)`; armed whenever both sensors are trusted, independent of the Auto-Level switch, suspended in bench-test mode), and the **§21.6 at-rest keeper in shadow only** (`REST-LEVEL(shadow)` log lines state what the feed-low/vent-high rule *would* do while parked at Lift — collecting confirmation data before actuation is built).

### 21.1 Why

The lift racks ~5° left/right and mechanical correction (torsion bars) failed. V2 splits the single pneumatic zone into one valve per tank and adds a second IMU so the controller keeps the lift level by steering air per side.

### 21.2 Roles — reference (master) / follower (slave)

- **Master = IMU #1** (existing, GPIO32) is the **reference frame**. Sole authority for lift *height*: position zones, go-to targets, angle trust, timeouts — the entire V1 FSM stays keyed to the master, unchanged.
- **Slave = IMU #2** (new, GPIO14 / HT3, UART0) answers one question only: *is my side where the master's side is?* The slave is corrected **to match the master**, never the reverse.
- **Physical side labels (decided + field-verified 2026-07-18): master = STARBOARD, slave = PORT.** UI entity names use the side terms via `side_master`/`side_slave` substitutions ("IMU Starboard OK", "Valve Port Position", "Calibrate Port: set LIFT", …); master/slave remain the internal role names in code and logs. **Valve A (Y2) = starboard tank and valve B (Y3) = port tank — confirmed at the dock** (if the plumbing is ever redone, swap the valve output pin numbers and feedback pin pairs in the substitutions; leveling requires valve A = master's side).
- Because the frame racks, "level" is compared in **calibrated-% space**, not raw angles: each sensor gets its own Lowered/Ready/Lift captures; `level error = slave_height_% − master_height_%` (positive = slave side HIGH). The racking is calibrated out.

### 21.3 Intervention thresholds (decided 2026-07-11)

- **Intervene** when the sides differ by more than the **Level Deadband** — **user-configurable, at most ~1° of arm angle**. (The knob is in % of span; with the current span, the 3 % default ≈ 0.8°, i.e. already inside the ≤1° intent. Keep the knob, document the degree equivalent.)
- **Hard stop** — divergence **beyond ~5°** between the two sides is a *something-is-mechanically-wrong* condition: **FAULT + make-safe (both valves closed) + alarm notification**. Air cannot fix a 5° split; mechanics is the problem. This is the existing Tilt Critical default (5°) applied to the two-IMU difference; also user-configurable, but it must never be silently disabled.

### 21.4 Hardware deltas vs V1

| Item | V1 | V2 |
|---|---|---|
| Vent/fill valve | 1 × auto-return on Y2 | valve A (master side) Y2 **+ valve B (slave side) Y3** (Y3 was spare) |
| Plumbing | single zone | **one line per tank**; blower feeds each side through a **check valve** (no shared open manifold — prevents the heavy side pushing its air to the light side) |
| IMU | WT901 TTL on GPIO32 (UART1) | + second WT901 TTL on **GPIO14 (HT3)** using **UART0** |
| Valve feedback | CR5-02 on DI1 (closed) / DI2 (open) — valve A | + valve B CR5-02 on **DI3/X03 (closed, yellow)** / **DI4/X04 (open, green)**, common (white) → same DI COM (added v2 rev C) |
| Logger | serial UART0 | **`baud_rate: 0`** (serial logging off; UART0 freed). Logs remain via API/web/`esphome logs` |
| Water temp | DS18B20 GPIO33 | unchanged |

Cable run to IMU #2 is plain 3.3 V TTL @ 9600 — bench-test at the real cable length (shielded twisted pair, away from blower AC). Fallback: WT901-RS485.

### 21.5 Control — leveling is a bias layer, not a new FSM

States 0–6 are untouched. With one blower, the only actuator is *which valve is open*, so the in-motion rule is: **throttle (close) whichever side is ahead**.

Throttle cases (`err = slave% − master%`):

| Move | err > +deadband (slave high) | err < −deadband (slave low) |
|---|---|---|
| RAISING | close **slave** (blower feeds master) | close **master** (blower feeds slave) |
| LOWERING | close **master** (slave vents alone) | close **slave** (it waits) |

Anti-chatter: level error is EMA-filtered (~3 s; waves largely cancel in the difference anyway), throttle engages above the deadband, releases inside deadband − hysteresis, and each decision holds a minimum time (valves take seconds to travel — corrections are coarse, patient pulses). The throttle never closes both valves (single-valued by construction).

### 21.6 At-rest leveling — raise the low side or lower the high side? (guard rails DECIDED 2026-07-19; shadow validating)

The 21.5 throttle only acts *during* moves. A list that develops **while parked at Lift** needs its own keeper, and the anchor is the **standard Lift height on the master (reference) IMU** — leveling must not walk the overall height away from the setpoint.

**Direction rule (confirmed by the 2026-07-19 field event):** if the reference height is *at or below* target → **feed air to the low side** (a top-up that also levels); if the reference is *above* target with a list → **vent the high side** (level and height both move toward setpoint, no air spent). First live validation: empty lift at 103% with port +4.6% high — Mike vented port manually, exactly what the rule prescribes.

**Guard rails (decided with Mike, 2026-07-19):**

1. **Own trigger, NOT auto-maintain's.** Auto-maintain is blind to a one-sided list (its observer watches master height only) and its `do_goto` top-up no-ops when the master is already at target. The keeper triggers on the **level error** instead: past its trigger threshold, sustained **~30 s** (the difference signal is already wave-cancelled — the 60 s sag persist is not needed and must not gate this).
2. **Trigger threshold 3% to start** (rest noise floor measured ±1.5% on 2026-07-19; each false trigger burns a pulse from the budget, so rest stays looser than the in-move deadband). Own slider when implemented (`Rest Level Trigger %`); tighten from profile data.
3. **Priority: auto-maintain first.** If the master height is below the sag deadband, the normal top-up raise (with §21.5 throttling riding inside it) fixes height + level together; the keeper acts only when height is fine but level isn't.
4. **Pulse primitive, bounded:** feed = blower ON + low side's valve only, ≤ ~45 s per pulse; vent = high side's valve only, ≤ ~10 s per pulse (venting is fast). Exit early when the moving side re-enters the band. Absolute blower cap backstops feeds.
5. **Vent floor:** a vent pulse aborts if the master would drop below the Lift band (target − zone tol) — never trade height for level; below that it becomes a feed case anyway.
6. **Progress check on feed:** the fed side must actually move within the pulse, else abort + flag (stuck valve / leak) — the leaking-tank "feed forever" failure lands here and in the budget.
7. **Budget + lockout:** corrections counted per window (start: 4 per 24 h) → lockout + fold into Lift Problem, mirroring auto-maintain's pattern.
8. **Eligibility:** HOLD + at-Top zone + both IMUs trusted + not bench/bypass. §21.3 hard stop bounds everything, as always.

**In-move deadband decision (same session):** Level Deadband set to **2%**. The 3% default was sized for the pre-install ~26° span estimate (3% ≈ 0.8°); on the real ~45° span, 3% = 1.35° — looser than the §21.3 "≤1° of arm" intent. 2% ≈ 0.9° restores it. Hysteresis 1% (release at 1%), min-hold 15 s unchanged; watch throttle-flip counts in profile runs for hunting. **Maintain Sag Deadband set to 3%** (top-up when the lift sags below ~95% of the 98% target).

### 21.7 FSM interactions

- **Two-sided completion:** a Ready/Lift/Max move ends when the master is at target **and** |level error| ≤ deadband. While the slave catches up, the master's valve is throttled shut so the master holds at target. A catch-up timer faults (`level_fail_catchup`) if the slave can't close the gap — likely a stuck valve B or a real leak. Lowered-target moves skip the gate: LOWERED_VENT leaves both valves open and both sides reach bottom anyway.
- **Feed-aware stall:** during RAISING with the master valve throttled, all air goes to the slave — the master correctly stops moving. The stall detector tracks progress on whichever side is being fed (in that sensor's own sign) and resets its tracker on every throttle change.
- **Auto-maintain:** unchanged, master-based. A top-up is a normal go-to raise; the leveling layer rides along inside it.
- **Manual moves** (angle untrusted): leveling fully disabled, both valves ganged — V1 behaviour.

### 21.8 Degraded modes

| Condition | Behaviour |
|---|---|
| Auto-Level switch OFF | valves ganged (= V1); decisions still logged as `LEVEL(shadow)` — observe-first, same as auto-maintain Phase 1 |
| Slave stale / implausible (`angle2_trusted` false) | leveling + completion gate disabled, valves ganged; "Level Sensor OK" goes off |
| Master trust lost mid-move | V1 rule: auto move stops (unchanged) |
| Sides diverge past the §21.3 hard stop | FAULT + make-safe (both valves closed) + alarm — mechanics, not air, is the problem |
| Slave can't catch up in time | `level_fail_catchup` FAULT |

### 21.9 Calibration (six captures + level check)

1. True Lowered → capture LOWERED on **both** sensors.
2. Ready (boat floating level — shim/ballast if needed) → capture READY on both.
3. Lift height, verified level by eye/level tool → capture LIFT on both.
4. Verify "Level Error" reads ≈ 0 % at each setpoint — the ~5° racking is now calibrated out.

### 21.10 New entities (v2 only)

Switches: `Auto-Level` (default OFF; shadow-logs when off), bench `Valve Port (Y3)`. Numbers: `Cal Port Angle — Lowered/Ready/Lift` (slave captures); Level Deadband (%), Level Hysteresis (%), Level Min Hold (s), Level Catch-up Timeout (s). Sensors: `Arm Angle Port`, `Height Port`, `Level Error (%)`. Binary: `IMU Port OK`. Text: `Level Status`, plus the combined `Valve Positions` line (both valves' real feedback positions). Buttons: `Calibrate Port: set LIFT / READY / LOWERED`.

### 21.11 Bring-up plan

1. **Bench:** valve B relay test; IMU #2 parse at real cable length; both angles live; bench cal load (loads both sensors).
2. **Dock, Auto-Level OFF:** normal V1-style moves on the new plumbing; watch `LEVEL(shadow)` logs + Level Error through several cycles; confirm sensor signs.
3. **Dock, Auto-Level ON:** first with someone at the dock; verify catch-up behaviour, tune deadband/hold; then normal service.

### 21.12 V2 open items

- [x] ~~Plumbing: per-tank lines + check-valved blower tee~~ **Done — installed for the 2026-07-18 cutover.**
- [x] ~~Confirm blower tolerates single-tank feed~~ **Confirmed in service** (leveling throttle ran live 2026-07-19; brief dead-heading is by design).
- [x] ~~Bench-verify IMU #2 TTL link / confirm sensor signs~~ **Done — both IMUs live and calibrated at the dock 2026-07-18.**
- [ ] **Implement the at-rest level keeper (§21.6)** — rule + guard rails decided 2026-07-19 (see §21.6); still shadow-only (`REST-LEVEL(shadow)` logs). Implement the pulse primitive + guards after boat-on/off operational profiles confirm the shadow decisions.
- [x] ~~Align the v2 YAML with rev H of V1~~ **Done (v2 rev B, 2026-07-11)** — rebuilt from rev-H V1 + the leveling layer; named constants, computed-once slave height (`have_cal2`/`slave_pct`), corrected bench-cal signs, §21.3 hard stop implemented.
- [x] ~~Decide cutover naming~~ **Decided 2026-07-18: keep `boat-lift-cobalt`** — HA history, dashboards and stored calibrations carry over; the project version string + device comment identify V2 firmware. Consequence: the `-v2`-name OTA guard is retired, and **flashing the plain-V1 yaml back onto the split plumbing is NOT hardware-safe** (valve B never opens → the slave tank can fill but never vent). Rollback = revert plumbing or use a V1-logic build that still drives Y3.
- [x] ~~Port to V1: sign-aware stall progress.~~ **Done (V1 rev I, 2026-07-11).** Found during the rev-B rebase: V1's stall detector counted progress as the angle *increasing*, but on the confirmed mounting a raise drives the angle *negative* — a raise longer than Stall Grace + Stall Timeout would false-fault `stall_no_progress`. Rev I measures progress in the direction of the calibrated span (`(angle − last) × sign(span)`), same as V2. **Stall defaults are deliberately permissive until baselines exist** (grace 60 s / timeout 120 s / min progress 0.2°, in both V1 and V2): raise speed differs a lot loaded vs empty and no §17.4 move data is recorded yet, so the detector should only catch a truly dead raise — the 5-min absolute blower cap stays the hard backstop. Tighten from data. *(The knobs persist in flash: after OTA, nudge the three sliders once — stored values are not overwritten by new defaults.)*

### 21.13 V1 design constraint until cutover (decided 2026-07-11)

Perfect the one-valve system first; every V1 change must stay V2-portable:

- **FSM state numbering frozen** (V2 reuses states 0–6; retired slot 1 stays reserved).
- **`apply_outputs` stays the single output writer** — V2 extends it with the per-side throttle.
- **Y3 and GPIO14 stay unassigned** in V1.
- Prefer changes expressed as **policy over the existing FSM** (like auto-maintain) — those port unchanged.
- **Port V1 fixes to `boat-lift-cobalt-v2.yaml`** as they land, so cutover is a hardware event, not a firmware rewrite.

## 22. Panel link (RS485) — as built

A wired RS485 link (GPIO13 TX / GPIO16 RX, **9600 8N1**, auto-direction transceiver) connects the lift to the dock touch panel. Implemented in firmware since YAML rev "wired RS485 link" (2026-07):

- **STA heartbeat** (~2 Hz, lift → panel): state token + height % + problem/trust flags + water temp + the human status line.
- **CMD parser** (panel → lift): `req=` tokens map onto the **same `request_*` intent scripts** a dock button uses — the panel cannot bypass the FSM or the safety supervisor. Telemetry + intents only; no safety logic depends on the link.

Protocol details: **`boat_lift_link_protocol.md`**. Panel UI: **`boat_lift_panel_design_revA.md`**.

---

*End of Revision F. The document matches the deployed firmware (`boat-lift-cobalt-v2.yaml` rev G, flashed 2026-07-20): V2 live on the dock with auto-leveling, the tuned boat-presence classifier, field-proven auto-maintain, a fail-safe blower chain, and the automation-facing status trio. Next: the descent-rate/seal-failure alarm (§13, data in hand), the §21.6 rest keeper out of shadow, and stall-threshold tightening from the recorded baselines.*
