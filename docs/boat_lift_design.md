# UltraLift Auto — Boat Lift Control Design

**Project:** ESPHome automation for a HydroHoist UltraLift UL2 8800 hydropneumatic boat lift (dual tank, two valves, two IMUs).
**Controller:** KinCony KC868-A16 (ESP32) — live config [`boat-lift.yaml`](../boat-lift.yaml).
**Companions:** [`adr.md`](adr.md) (design decisions) · [`boat_lift_link_protocol.md`](boat_lift_link_protocol.md) (RS485 panel link) · [`boat_lift_panel_design_revA.md`](boat_lift_panel_design_revA.md) (touch panel UI).

Firmware/control design for single-button go-to-position operation with local safety supervision, dual-tank auto-leveling, autonomous height maintenance, position sensing, dock buttons with status LEDs, and Home Assistant visibility. Safety-critical behaviour stays entirely on the A16; the network is observability only.

---

## 1. Purpose & Scope

Automate a dual-tank HydroHoist UltraLift so an operator (or Home Assistant) can send the lift to named setpoints. The controller:

- Picks raise vs lower automatically
- Steers air per tank to keep the cradle level while moving
- Holds parked height (and, at Lift, level) against slow air loss
- Infers boat presence from raise physics to guard the roof-clearance setpoint
- Exposes status for HA automations without putting HA in the safety loop

---

## 2. The Lift Mechanism (control-relevant facts)

(Source: HydroHoist UL2 Installation Manual.)

- **Raises** by a **blower pushing air into the tanks** (positive pressure). Not a vacuum.
- **Lowering is passive:** blower off, valves open → air vents under tank pressure → descends.
- **Hold** = valves sealed.
- This installation is a **split pneumatic zone**: one auto-return valve per tank, shared blower through a check-valved tee. Side-to-side list can be corrected by steering which valve is open.
- Blower load: **115 VAC, 11 A**, high inrush; OEM has GFCI + thermal + ~15-min auto-off.
- A manual PVC-tube exhaust procedure is the no-power manual backstop.
- **Control valves (as installed):** auto-return valves driven one line each (**Y2** starboard / **Y3** port) — energized = OPEN, de-energized = returns CLOSED. Measured travel ≈ **7 s to open, ≈ 2.3 s to close**. Each carries CR5-02 maintained aux contacts reporting fully-open / fully-closed (§7.1).

Position is sensed via **arm angle** on each side (four-bar parallelogram). Master arm angle → height by two-point calibration; slave angle is compared in calibrated-% space for leveling.

### 2.1 Control truth table (steady-state)

| Mode | Blower | Valves |
|---|---|---|
| Raise | ON | OPEN (or throttled per side — §16) |
| Hold | OFF | CLOSED |
| Lower | OFF | OPEN (or throttled per side — §16) |

The valves are OPEN in both raise and lower; the **blower relay is the only difference** for direction. Blower and valves are commanded **independently and simultaneously**: a raise energizes both at once, and the blower briefly dead-heads while a valve finishes opening. No sequencing interlock.

### 2.2 Sensor axis assignment

Each WT901 reports two tilt axes:

- **X-axis (Roll) = the height/position axis.** `current_angle` tracks Roll; all height %, setpoints, calibration, and go-to logic derive from X on the **master** IMU.
  **Sign convention (field-confirmed):** fully **lowered = positive** angle; raising swings the angle **negative**. Height % handles the negative span transparently. The one-sided Lowered zone ("at the lowered angle *or settled beyond it*") depends on this sign — revisit if a sensor is remounted.
- **Y-axis (Pitch) = observe-only “sensor moved” axis.** Pitch should not change as the lift swings; drift implies the sensor was knocked or loosened. The derived **`Lift Tilt`** sensor (pitch minus a captured reference) is the legacy consumer; the two-IMU **Level Error** is the primary list signal and is expected to supersede Guard B.

---

## 3. Hardware Architecture (as built)

| Role | Part | Interface to A16 |
|---|---|---|
| Controller | KinCony **KC868-A16** (ESP32, esp-idf) | — |
| **Angle sensor (starboard / master)** | **WitMotion HWT901B-TTL** (TTL, native `0x55` protocol, 9600) | **GPIO32** (UART RX, read-only). VCC 12 V, GND shared. TTL is 3.3 V → direct. |
| **Angle sensor (port / slave)** | second **HWT901B-TTL** | **GPIO14 / HT3** (UART0 — serial logging off, `baud_rate: 0`) |
| Water temp | **DS18B20** (1-Wire) | **GPIO33** + 4.7 kΩ pull-up to 3.3 V. *Scans at boot only — reboot to detect.* |
| Blower switch | RIB2401B pilot + NT90 12 V relay (drives 115 VAC blower). **NO path — fail-safe: coil de-energized = motor OFF** | **Output Y1** (drives relay coil) |
| Control valves | auto-return motorized ball valves (energized = open), manual override | **Y2 = starboard (valve A)**, **Y3 = port (valve B)** |
| Valve feedback | CR5-02 maintained aux contacts (common = white → DI COM) | Valve A: **DI1 = CLOSED (yellow), DI2 = OPEN (green)**; valve B: **DI3 = CLOSED, DI4 = OPEN** |
| Buttons (×4) | 19 mm illuminated momentary, NO + LED ring | switch → DI, LED ring → output Y |
| Panel link | RS485 to dock touch panel (§18) | **GPIO13 TX / GPIO16 RX @ 9600** |
| Power | Mean Well 12 V DIN | 12 V rail |

*(No hardware top-limit switch is fitted — raising stops on the calibrated target + the absolute blower cap.)*

### 3.1 Dock button wiring (DMWD 19 mm)

- **Switch (NO) = YELLOW + WHITE** (open at rest, closes on press). Spare NC = GREEN (unused). **Use NO** — fail-safe (broken wire = not pressed).
- **LED ring = RED (+) / BLACK (−)**, ~12 V (built-in resistor).

### 3.2 Bill of materials

Everything below is off-the-shelf; the only specialty part is the lift itself.

| Qty | Item | Role |
|---|---|---|
| 1 | **KinCony KC868-A16** — ESP32 relay/IO board (16 opto inputs, 16 outputs, RS485) | the controller |
| 2 | **WitMotion HWT901B-TTL** — 9-axis inclinometer, 0.05° XY accuracy, Kalman-filtered | arm angle, one per side |
| 2 | **Motorized ball valve, 1″ NPT, 304 SS, full port** — 2-wire auto-return (normally closed), quick-release actuator, IP67, 12–24 V | per-tank vent/fill valves (aux feedback contacts) |
| 1 pk (2) | **EPLZON NT90 power relay** — 12 VDC coil, 30/40 A SPDT (1NO 1NC), flange mount | blower motor switching (115 VAC, 11 A, high inrush) |
| 4 | **DMWD 19 mm momentary push button** — 12–24 V ring LED, 1NO 1NC, waterproof | dock control panel (Lift / Ready / Stop / Lower) |
| 1 | Outdoor hinged junction box (~13″×9″×6″) with mounting panel | controller enclosure |
| 1 | Smaller IP65 hinged ABS box with cable glands | remote-side electronics (IMU/valve wiring) |
| — | **1″ NPT 304 SS street elbows + 3-way tee** | valve plumbing / check-valved blower tee |
| — | **1-¼″ reinforced suction/discharge hose** | blower-to-tank air lines |
| 1 | **Cat6 outdoor direct-burial cable** | dock run (panel link / low-voltage signals) |
| 1 | **DS18B20** waterproof probe | water temperature |
| 1 | **Mean Well 12 V DIN supply** | logic + valve/LED power |

**A16 specifics:** I²C GPIO4/5; opto inputs PCF8574 @ **0x22** (DI1–8) / **0x21** (DI9–16); outputs PCF8574 @ **0x24** (Y1–8) / **0x25** (Y9–16). Inputs are dry-contact-to-GND, `inverted: true`. Outputs drive DC loads (relay coils, LED rings) off the output bank's 12 V input. DI15/X15 was found dead in the field — Ready uses **DI11** instead.

---

## 4. Control Architecture: Supervised FSM

A small **go-to-position FSM** beneath an always-on **safety supervisor** that can force FAULT from any state. Three structural rules:

1. **Intents, not actions.** Buttons/HA emit requests; one validated path changes state.
2. **Outputs are a pure function of state** — blower/valves/LEDs set in one place (`apply_outputs`), re-asserted every tick. Leveling is a **bias layer** over those outputs, not a second FSM.
3. **The supervisor preempts** — monitors force FAULT (or safe-hold) ahead of user intent.

`make_safe` = both valves commanded closed, blower off; it backs both HOLD and FAULT.

---

## 5. Position Modes & Setpoints

Four **calibrated setpoints**, each a target the lift can be sent to:

| Mode | Button (LED) | Position | Setpoint |
|---|---|---|---|
| **Lift** | White | Everyday stored position | **`Lift Target %`** (default 98 % of the calibrated span — a percent target, not a captured zone) |
| **Lift Max** | *(HA/web/panel only)* | Maximum out of water, beyond Lift | `cal_max` (captured angle) |
| **Ready** | Orange | Almost down — lowered, not yet floating | `cal_ready` (captured angle) |
| **Lowered** | Green | All the way down (boat floating free) | `cal_lowered` (~0 %) |

Plus red **Stop** (no position). Height % is derived linearly between `cal_lift` (100 %) and `cal_lowered` (0 %); Ready and Lift Max are captured as their own angles. **Naming:** buttons/UI say *Lift / Ready / Lower*; firmware internal calibration ids remain `up` / `float` / `down` / `max` for flash-persistence compatibility.

The **current resting mode** is derived from the live angle vs each setpoint (within the Zone Tolerance band, §6.2): *Lifted (max) / Lifted / Ready / Lowered / Between*.

### 5.1 Boat-presence classifier + Lift Max roof guard

**Why:** Lift Max (~148 % of the everyday span) can drive a boat’s tower into a boathouse roof — it must only run with the boat **off** the lift. There is no presence switch; presence is inferred from physics.

**How:** presence is classified on **raise cycles only**, from the early climb rate — a loaded raise is far slower than an empty one (example field rates: loaded **~0.53 %/s**, empty **~1.5–1.7 %/s**). The classifier times the first 15 %-points of climb and compares against the `Empty Raise Rate Min` slider (default **1.20 %/s**, biased high so a loaded raise cannot fake EMPTY). Fail-safe polarity: unknown/unclassified = **boat ON**, and boat ON blocks `request_goto_max`. Presence resets to presumed-ON wherever the boat could change (LOWERED_VENT, bypass entry); the latch persists across reboots.

**Decide en route:** a Max press from a LOW start (<50 %) with presence unknown is allowed to launch — the classifier resolves on the way up, and ANY non-EMPTY outcome demotes the in-flight Max to the boat-safe Lift target. High starts stay hard-blocked without a positive EMPTY verdict from the previous raise. Leveling interaction: a slave-side throttle during the timing window inflates the master's apparent rate, so that direction **aborts** classification for the raise (the previous latch holds).

A `Boat Present` occupancy sensor exposes the latch. Display-only `Bunk Height` reports inches above the Lowered cal from arm geometry.

---

## 6. Go-to-Position State Machine

Pressing a mode button sends the lift to that setpoint; the controller **chooses the direction automatically**.

### 6.1 States

| State | Blower | Valves | Meaning |
|---|---|---|---|
| **HOLD** | OFF | CLOSED | Resting (at a mode, or between). |
| **RAISING** | ON | OPEN / throttled | Pumping air in; rising toward target. Blower + valves energize **together**. |
| **LOWERING** | OFF | OPEN / throttled | Venting; descending toward target. |
| **LOWERED_VENT** | OFF | **OPEN** | Resting at Lowered with vents left open — the lift keeps settling indefinitely. New commands accepted; **Stop seals the valves** (→ HOLD). |
| **FAULT** | OFF | CLOSED | Latched safe state + reason. |
| **BYPASS** | OFF | **OPEN** | Manual override (§6.6): valves open, blower off, FSM idle. |

*(Numeric FSM slot 1 is reserved/retired — formerly a valve-opening interlock state.)*

### 6.2 Direction selection (on a mode-button request)

Accepted from **rest** (HOLD / LOWERED_VENT) **or mid-move** (RAISING / LOWERING) — last mode press wins. Stop is the cancel path (→ HOLD). FAULT / BYPASS refuse go-to.

```
target = setpoint(mode)                 # Lift Target % / cal_max / cal_ready / cal_lowered
if  height < target - tol:  raise  ->  RAISING   (blower + valves ON together)
elif height > target + tol: lower  ->  LOWERING  (valves ON, blower off)
else:                       already there -> HOLD (or LOWERED_VENT for Lowered)
```

So **Ready (and Lift Max) are reachable from above or below**, and a mid-move press may **reverse** direction if the new target is on the other side of the live height.

Same-direction retarget only updates the destination (stall / blower / presence trackers keep running). A reverse (raise↔lower) re-arms the move-scoped timers and, on a fresh raise, the boat-presence classifier.

Position checks use **one captured setpoint per mode + one configurable band**: **`Zone Tolerance (°)`**. The band defines both "already there" and "at mode":
- **Lowered:** one-sided — angle > `cal_lowered` − tol (at the lowered cal *or settled beyond it*; valid for the confirmed sensor sign, §2.2).
- **Ready / Lift Max:** captured angle ± tol.
- **Lift:** height ≥ `Lift Target %` − tol (tolerance converted °→% through the calibrated span).

### 6.3 Stopping

- **RAISING:** stop when the target zone is reached → HOLD. (Backstops: stall detector and absolute blower cap.)
- **LOWERING:** stop when the target zone is reached → HOLD — **except a Lowered target**, which rests in **LOWERED_VENT**. The `Lower Timeout` timer backstops a descent that never confirms its zone.
- **Stop button:** any moving state → HOLD; from LOWERED_VENT it seals the vents → HOLD. Mode buttons do **not** cancel — they retarget (§6.2).
- **Supervisor:** → FAULT (hard) or **auto-stop on loss of angle trust** (§6.5).

### 6.4 Preconditions

A position-targeted move requires: **calibrated**, a **trusted master angle** (§9.4), and **not faulted**.

### 6.5 Degraded / manual mode (angle not trusted)

When the master angle is **offline** or **out of range**, the controller drops to **manual-recovery mode** rather than a latched fault:

- A position-targeted move in progress is **STOPPED** to HOLD.
- **Red LED flashes** (critical); **Lift → manual RAISE jog**, **Lowered → manual LOWER jog**, **Ready refused**, **Stop always live**.
- Manual jogs run **without position feedback**, bounded only by the **absolute blower cap** (up), **lower-timeout** (down), and **Stop**.
- Returning to a trusted angle restores normal go-to automatically.
- While untrusted, leveling is disabled and both valves are ganged.

### 6.6 Bypass mode (manual override)

Hands-off override that **opens both valves and ensures the blower is off**, then does nothing.

- **Outputs:** valves **OPEN**, blower **OFF**. The lift is **vented** — it will not hold on the pneumatics.
- **Entry:** hold the red Stop button ~3 s, or toggle **Bypass Mode** ON (web/HA).
- **Exit:** short-press Stop, or toggle the switch OFF → **HOLD**.
- **LEDs:** all OFF in bypass — a dark panel means bypass.
- **Status:** Lift Status shows bypass; **Lift Problem = ON** while bypassed.

---

## 7. Output Mapping

```
apply_outputs(state):                         # the ONLY writer of blower/valves
  RAISING:              valves OPEN*, blower ON      # *may throttle one side (§16)
  LOWERING:             valves OPEN*, blower OFF
  LOWERED_VENT:         valves OPEN,  blower OFF     # resting, vents left open
  BYPASS:               valves OPEN,  blower OFF
  HOLD / FAULT:         valves CLOSED, blower OFF    # make_safe
  (suspended entirely while Bench Test Mode is ON)
```

### 7.1 Valve position feedback (CR5-02 aux contacts)

Maintained dry contacts report each valve’s **real** end-stop position, independent of open-loop travel timing. Debounced 100 ms. Derived per-valve text: `OPEN` / `CLOSED` / `MOVING` / `FAULT` (both contacts on). Combined **Valve Positions** line shows both sides.

Uses today (**display/diagnostic — not an FSM input yet**):
- Live valve state on Diagnostics, independent of Y2/Y3 commands.
- **Manual-operation detector:** if command and feedback still disagree after a grace window (`Valve Travel Time` + 2 s) and the mismatch persists ~2 s, status shows **"Manual valve"**.

Future option (not done): use end-stops as stuck-valve fault triggers.

---

## 8. Button LED Scheme

| LED | SOLID when | FLASHING when | OFF otherwise |
|---|---|---|---|
| Lift (white) | resting **at Lift** | moving with **target = Lift** | |
| Ready (orange) | resting **at Ready** | moving with **target = Ready** | |
| Lowered (green) | resting **at Lowered** | moving with **target = Lowered** | |
| Stop (red) | **moving (in operation)** | **critical** — fault OR angle not trusted | resting & healthy |

- Position LED **solid** = resting in that mode; **flashing** = heading to that mode.
- **Red solid** = "in operation — press to stop." **Red flashing** = "critical."
- In degraded/manual mode white (Lift) and green (Lowered) are the live manual controls.
- **Bypass:** every LED OFF.
- Cadences: position LEDs flash **slow** (~0.8 Hz) while moving; **fast flash** (~2.5 Hz) for faults/critical on red. Angle-sensor-**offline**: ~3 s fast-flash burst, then ~10 s dark, repeating.

---

## 9. Health & Status Model

### 9.1 What we monitor

- **Angle sensors** — ON = fresh valid `0x53` frames (freshness window ~3 s), per IMU.
- **Water-temp sensor (DS18B20)** — present/reading.
- **Equipment / ESP health** — uptime, WiFi signal, hardware.
- **Lift errors** — stall (raising, no progress), blower absolute-runtime cap, level divergence, valve/position anomalies → FAULT with reason.

### 9.2 Roll-up entities (status trio)

- **`Lift Activity`** *(machine-friendly — what the lift is DOING)*: exactly one of `Idle / Raising / Lowering / Leveling / Fault / Bypass`. `LOWERED_VENT` reads as `Idle`.
- **`Lift Position`** *(machine-friendly — WHERE it is)*: exactly one of `Lowered / Ready / Lifted / Lifted Max / Between / Unknown`.
- **`Lift Status`** *(human-readable line, feeds the panel `msg=`)* via a priority ladder:
  1. `FAULT — <reason>`
  2. `BYPASS — valve open, blower off (manual override)`
  3. Moving → `MANUAL raising…` (untrusted) or `Raising -> Ready` (go-to; **no live % embedded**)
  4. `Lowered — vent open` (LOWERED_VENT)
  5. `Manual valve` (feedback disagrees with closed command, §7.1)
  6. `Angle sensor OFFLINE — manual control` / `Angle OUT OF RANGE — manual control`
  7. `Not calibrated`
  8. Resting → `Lowered`, `Ready`, `Lifted`, `Lifted (max)`, `Between …`, or `Holding`

Short tokens exist for **exact-match Home Assistant automations**. Embedding a live percentage in status floods the HA recorder.

- **Lift Problem** (binary, `problem`) — ON for fault / bypass / **angle not trusted** / uncalibrated.

### 9.3 UI tiers (web_server sorting groups)

**1 Control** · **2 Status** · **3 Configuration** · **4 Advanced Tuning** · **5 Diagnostics** · **6 Bench**. Bench binary inputs are `disabled_by_default` in HA.

### 9.4 Angle trust ladder

Only **trusted** master angle allows automatic go-to moves:

| Rung | Test | Behaviour |
|---|---|---|
| **OFFLINE** | no fresh frames within `Angle Freshness` (~3 s) | manual recovery; sensor OK = off |
| **OUT OF RANGE** | data fresh, but live angle outside calibrated span ± `Angle Plausibility Margin` (default 10°) | manual recovery; auto-move stopped |
| **SENSOR MOVED** *(Guard B, Y-axis — placeholder)* | Pitch drifts > `Pitch Drift Limit` from commissioning reference | decoded, not yet acted on |
| **TRUSTED** | fresh + plausible | normal go-to |

Implemented as `angle_valid` (fresh + inside ±95°) → `angle_trusted` (valid + plausible). Loss of trust mid-move **auto-stops** to HOLD. The slave IMU runs the same ladder independently (`angle2_valid` / `angle2_trusted`); leveling degrades to ganged valves when the slave loses trust.

Size the `angle_valid` gate to the *mounted* sensor. With the confirmed mounting (arm swing well inside ±90°, lowered = positive), the gate is **±95°**. Remount → re-check this gate and the one-sided Lowered zone (§6.2).

---

## 10. Calibration & Persistence

- **Four master captures** on the lift: **Lift**, **Lift Max**, **Ready**, **Lowered** — each records the current master angle. Positions are setpoints plus the `Zone Tolerance` band.
- **Three slave captures** at the same physical positions (Lowered / Ready / Lift) so frame racking is calibrated out in % space (§16.2).
- Each capture is also an **editable number** (Advanced Tuning) so a setpoint can be nudged or restored without re-running the lift. A **Calibration Summary** line shows every zone edge.
- Height % is linear between Lift and Lowered (negative span — lowered is the *high* angle, §2.2).
- A **capture LEVEL ref** button records the reference pitch for the tilt proxy (§17).
- Stored in flash (`restore_value: yes`).
- Target moves refused until calibrated; the plausibility window (§9.4) derives from these captures.

---

## 11. Safety Contract (invariants)

- Blower ON **only** in RAISING and not faulted. Blower and valves energize together; brief dead-heading is accepted by design.
- HOLD/FAULT: valves commanded CLOSED, blower OFF. (LOWERED_VENT/BYPASS: valves OPEN, blower OFF.)
- **Absolute blower runtime cap** (`Blower Max Runtime`, all modes incl. manual test): the blower can never run longer than this. *Must exceed real full-raise time before live use (OEM auto-off ~15 min).*
- RAISING requires angle progress after a grace window, or FAULT (stall). Stall progress is **sign-aware** and **feed-aware** during leveling throttle.
- **Automatic moves require a trusted master angle** (§9.4); loss of trust mid-move **auto-stops** to HOLD.
- **Level divergence hard stop** (§16.3): filtered two-IMU difference beyond `Tilt Critical` → FAULT + make-safe.
- **Power-up → safe** (valves closed, HOLD). **Power-loss → safe drift** (vents down to float). **Network loss → no change in safe behaviour.**
- **Bench Test Mode must be OFF for normal service** (suspends FSM output control and ignores button intents).

---

## 12. I/O Map (as built)

| A16 terminal | Signal | Notes |
|---|---|---|
| GPIO32 | IMU #1 (starboard/master) TX | TTL `0x55` parse, read-only |
| GPIO14 (HT3) | IMU #2 (port/slave) TX | UART0 — serial logging off (`baud_rate: 0`) |
| GPIO33 | DS18B20 water temp | + 4.7 kΩ pull-up |
| GPIO13 / GPIO16 | RS485 panel link TX / RX | 9600 8N1, §18 |
| Y1 | Blower relay coil | NT90 on the **NO path — fail-safe** (de-energized = motor OFF), `inverted: true` |
| Y2 | Valve A / starboard (ON = open) | |
| Y3 | Valve B / port (ON = open) | |
| Y4 / Y5 / Y6 / Y7 | Button LEDs: Lift / Ready / Stop / Lowered | |
| DI12 / DI13 / DI11 / DI16 | Buttons: Stop / Lift / Ready / Lowered | Ready on DI11 (DI15 dead) |
| DI1 / DI2 | Valve A feedback: CLOSED / OPEN | CR5-02 aux contacts, §7.1 |
| DI3 / DI4 | Valve B feedback: CLOSED / OPEN | same pattern |

---

## 13. Open Items

Still open:

- **Descent-rate / seal-failure alarm.** Triggers: height falling while both valves are commanded closed; rate far above the known lower profile during a commanded lower.
- **Can't-hold / immediate re-sag FAULT** (topped up and sagged again within a short window) — alert without disabling keepers.
- **Tighten stall thresholds** from recorded baselines (still permissive: grace 60 s / timeout 120 s / 0.2°; 5-min blower cap remains the hard backstop).
- **Diagnose command-to-valve-motion delay** (~1–2 s press-to-sound; firmware path ≈100 ms — use §7.1 feedback timestamps).
- Decide manual jog: **latched-with-Stop** (current) vs **hold-to-run / deadman**.
- **Maintain-at-Ready policy** (float-away guard): whether sustained Ready sag should escalate to notify/FAULT.
- Decide whether valve feedback should ever gate the FSM (stuck-valve fault) or stay display-only.
- Retire the Lift Tilt pitch proxy once two-IMU level error covers the "sensor moved" case.

Field move profiles used for tuning live in `field-data/`.

---

## 14. Failure modes (control-relevant)

### 14.1 Wave-driven asymmetric air loss

Working mechanism:

1. Wave action works air out of **one tank** faster than the other.
2. That corner sinks → the **back of that tank lifts clear of the water**.
3. Water inside runs forward → buoyancy shifts → **more air escapes** (positive feedback).

Firmware role: **maintain height**, **correct list** via the dual-tank level layer, **detect + notify** when air cannot fix a mechanical problem (divergence hard stop).

### 14.2 Compression spiral (empty lift mid-stroke)

A sealed empty lift mid-stroke is **not stable**. Measured: sealed at 92 % → sank to 33 %; sealed at 96 % → 37 %. As the lift settles, tank air compresses under the growing water column, buoyancy falls, and the descent runs away until it rebalances around ~33 %.

Consequences:

- No stable sealed states for an empty lift between roughly **98 % and 35 %**.
- Any automatic vent cutoff must trigger at **≥98 %** (valve close travel is ~2–3 s).
- Plunge profiles calibrate the §13 descent-rate/seal-failure alarm.
- A single-tank leak on an *empty* lift often self-limits as a small differential; height-based maintain remains the primary leak responder.

---

## 15. Maintain Height & Maintain Level

Hold the lift at its parked setpoint against slow air loss (**height**) and, at everyday Lift only, against developing list (**level**). Both are thin policies on the existing FSM. They inherit every backstop: blower runtime cap, stall detector, angle-trust auto-stop, level-divergence hard stop.

**Maintain Height** and **Maintain Level** switches **default ON**. There is **no sticky lockout** that disables keepers after N events — an unattended lift must not tip or sag because a counter tripped. Visibility is via **per-visit counters** on `Maintain Observe`.

### 15.1 Where each keeper runs

| Parked position | Maintain Height | Maintain Level (at-rest) |
|---|---|---|
| **Lift** | yes | **yes** (feed-low / vent-high pulses) |
| **Ready** | yes | no |
| **Lift Max** | yes | no |
| **Lowered** | never | never |

In-move throttle follows **Maintain Level** during any go-to, not only at Lift.

### 15.2 Height keeper

- **Observer always runs:** filters height, estimates sag rate and wave movement.
- **`Maintain Height`** (default ON): confirmed sag fires a **real top-up through `do_goto()`**. Arms while idle at Ready / Lift / Lift Max. Detector: settle delay → filtered height below `setpoint − deadband` for persist time → min interval between top-ups. Tuned defaults: sag deadband **3 %**, persist 60 s, min interval 12 min.
- **Up-only** — never auto-lowers.
- **Priority over rest-level:** if master height is below the sag deadband, any rest-level pulse aborts and height owns the recovery.

### 15.3 No sticky lockout — visit counters

**Runaway protection that remains:**
- Min interval between top-ups
- Absolute blower runtime cap
- Stall detector on raises
- Level-divergence FAULT
- Rest-level pulse caps (≤45 s feed / ≤10 s vent) + vent height floor + feed progress check

**Visibility:** when the lift arrives HOLD at a maintained zone different from the current visit, counters reset. `Maintain Observe` shows visit position, switches, top-up + level counts, sag rate, and arm state.

### 15.4 Notification policy

- **Notify immediately:** Lift Problem on; latched fault; angle offline/out of range; blower-cap or stall; level divergence hard stop.
- **Notify as warning:** high visit top-up or level counts; unusually negative sag rate.
- **Never auto-correct:** unknown position; untrusted angle; lowered/floating state; list at Ready or Lift Max (height only there).

### 15.5 Baselines to record at commissioning

- Lift / lower time with and without boat
- Normal top-up duration and overnight visit counts at Lift / Ready / Max
- Idle sag rate, wave peak-to-peak, and level-error range at rest

---

## 16. Dual-tank auto-leveling

Each tank has its own vent/fill valve and inclinometer. The controller keeps the lift level by steering air per side. Rationale: see [`adr.md`](adr.md).

### 16.1 Roles — reference (master) / follower (slave)

- **Master = IMU #1** (GPIO32) is the **reference frame**. Sole authority for lift *height*: position zones, go-to targets, angle trust, timeouts.
- **Slave = IMU #2** (GPIO14) answers one question: *is my side where the master's side is?* The slave is corrected **to match the master**, never the reverse.
- **Physical side labels:** master = **STARBOARD**, slave = **PORT**. UI entity names use side terms via substitutions; master/slave remain internal role names. **Valve A (Y2) = starboard** and **valve B (Y3) = port** — leveling requires valve A = master's side (swap pin substitutions if plumbing differs).
- Because the frame racks, "level" is compared in **calibrated-% space**: each sensor gets its own Lowered/Ready/Lift captures; `level error = slave_height_% − master_height_%` (positive = slave side HIGH).

### 16.2 Intervention thresholds

- **Intervene** when sides differ by more than the **Level Deadband** (configurable; intent ≈ ≤1° of arm).
- **Hard stop** — divergence beyond ~5° (`Tilt Critical`) → **FAULT + make-safe (both valves closed) + alarm**. Air cannot fix a large mechanical split.

### 16.3 Hardware (per-tank zone)

| Item | As built |
|---|---|
| Vent/fill valves | valve A (master/starboard) on Y2 + valve B (slave/port) on Y3 |
| Plumbing | one line per tank; blower feeds each side through a **check valve** |
| IMUs | WT901 TTL on GPIO32 (UART1) + GPIO14 (UART0) |
| Valve feedback | CR5-02 on DI1/DI2 (A) and DI3/DI4 (B) |
| Logger | `baud_rate: 0` (serial logging off; UART0 freed). Logs via API/web/`esphome logs` |

### 16.4 Control — leveling is a bias layer

States 0–6 are unchanged. With one blower, the only actuator is *which valve is open*: **throttle (close) whichever side is ahead**.

| Move | err > +deadband (slave high) | err < −deadband (slave low) |
|---|---|---|
| RAISING | close **slave** (blower feeds master) | close **master** (blower feeds slave) |
| LOWERING | close **master** (slave vents alone) | close **slave** (it waits) |

Anti-chatter: level error is EMA-filtered (~3 s); throttle engages above deadband, releases inside deadband − hysteresis, and each decision holds a minimum time. The throttle never closes both valves.

### 16.5 At-rest leveling — at Lift only

While parked at **Lift**, a list is corrected by the at-rest keeper. Ready and Lift Max are **height-only**.

**Direction rule:** if reference height is at or below target → **feed air to the low side**; if reference is above target with a list → **vent the high side**.

**Guard rails:**

1. Own trigger on **level error** past `Rest Level Trigger %` (default 3 %) for `Rest Level Persist` (default 30 s).
2. **Maintain Height first** — if master height is below the sag deadband, abort the rest pulse.
3. Pulse primitive: feed ≤ 45 s; vent ≤ 10 s; exit early when error re-enters the release band; min ~60 s between pulses.
4. Vent floor: abort if master would drop below the Lift band.
5. Progress check on feed (~15 s) or abort + log.
6. No sticky lockout — visit **level** counter increments; §16.2 hard stop remains the mechanical backstop.
7. Eligibility: HOLD + at Lift + both IMUs trusted + Maintain Level ON + not bench/bypass.
8. FSM stays in HOLD; `rest_pulse` biases `apply_outputs`. `Lift Activity` reads **Leveling** while a pulse is active.

### 16.6 FSM interactions

- **Two-sided completion:** Ready/Lift/Max moves end when master is at target **and** |level error| ≤ deadband. Catch-up timer faults (`level_fail_catchup`) if the slave cannot close the gap. Lowered-target moves skip the gate (LOWERED_VENT leaves both valves open).
- **Feed-aware stall:** during RAISING with the master valve throttled, progress is tracked on the side being fed.
- **Auto-maintain top-ups** are normal go-to raises; the leveling layer rides along.
- **Manual moves** (angle untrusted): leveling disabled, both valves ganged.

### 16.7 Degraded modes

| Condition | Behaviour |
|---|---|
| Maintain Level OFF | valves ganged; decisions still logged as `LEVEL(shadow)` |
| Slave stale / implausible | leveling + completion gate disabled, valves ganged |
| Master trust lost mid-move | auto move stops |
| Sides diverge past hard stop | FAULT + make-safe |
| Slave can't catch up in time | `level_fail_catchup` FAULT |

### 16.8 Calibration (six captures + level check)

1. True Lowered → capture LOWERED on **both** sensors.
2. Ready (boat floating level) → capture READY on both.
3. Lift height, verified level → capture LIFT on both.
4. Verify Level Error ≈ 0 % at each setpoint.

### 16.9 Key entities (leveling)

Switches: `Maintain Level` / `Maintain Height` (default ON), bench `Valve Port (Y3)`. Numbers: slave cal angles; Level Deadband / Hysteresis / Min Hold / Catch-up Timeout; Rest Level Trigger / Persist. Sensors: `Arm Angle Port`, `Height Port`, `Level Error (%)`, `Maintain Observe`. Binary: `IMU Port OK`. Text: `Level Status`, `Valve Positions`.

---

## 17. Observe instrumentation (always on)

Diagnostic entities, computed every 250 ms, **never write blower/valve**:

- **Lift Height (filtered)** — wave-stripped EMA (*Maintain Smoothing*).
- **Lift Wave P-P (60 s)** — peak-to-peak of raw height over 60 s.
- **Lift Sag Rate (%/h)** — slope of filtered height.
- **Lift Tilt (°)** — Pitch minus captured level reference (legacy; prefer Level Error).
- **Maintain Observe** — visit position, switches, counters, sag rate, arm state.

Tunable defaults (Advanced Tuning): height **3 % / 60 s / 12 min / 180 s / 20 s**; rest-level **3 % / 30 s**; Tilt Critical **5°**.

---

## 18. Panel link (RS485)

A wired RS485 link (GPIO13 TX / GPIO16 RX, **9600 8N1**, auto-direction transceiver) connects the lift to the optional dock touch panel:

- **STA heartbeat** (~2 Hz, lift → panel): state token + height % + problem/trust flags + water temp + human status line.
- **CMD parser** (panel → lift): `req=` tokens map onto the **same `request_*` intent scripts** a dock button uses — the panel cannot bypass the FSM or safety supervisor.

Protocol: [`boat_lift_link_protocol.md`](boat_lift_link_protocol.md). Panel UI: [`boat_lift_panel_design_revA.md`](boat_lift_panel_design_revA.md).

---

*Design decisions and rationale: [`adr.md`](adr.md).*
