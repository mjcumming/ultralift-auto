# UltraLift Auto — Boat Lift Control Design

**Project:** ESPHome automation for a HydroHoist UltraLift UL2 8800 hydropneumatic boat lift (dual tank, two valves, two IMUs).
**Controller:** KinCony KC868-A16 (ESP32) — live config [`boat-lift.yaml`](../boat-lift.yaml).
**Companions:** [`adr.md`](adr.md) (design decisions) · [`boat_lift_ui_reference.md`](boat_lift_ui_reference.md) (web UI & calibration reference) · [`boat_lift_link_protocol.md`](boat_lift_link_protocol.md) (RS485 panel link) · [`boat_lift_panel_design_revA.md`](boat_lift_panel_design_revA.md) (touch panel UI).

Firmware/control design for single-button go-to-position operation with local safety supervision, dual-tank auto-leveling, autonomous height maintenance, position sensing, dock buttons with status LEDs, and Home Assistant visibility. Safety-critical behaviour stays entirely on the A16; the network is observability only.

---

## 1. Purpose & Scope

Automate a dual-tank HydroHoist UltraLift so an operator (or Home Assistant) can send the lift to named setpoints. The controller:

- Picks raise vs lower automatically
- Steers air per tank to keep the cradle level while moving
- Holds parked height (and, at Lift, level) against slow air loss
- Infers boat presence from raise physics to pick the Lift shutoff (boat vs empty)
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
- **Why opening takes ~7 s (dock-observed 2026-07-26 — not a bug):** the relay energizes within one 250 ms control tick of the button press; the wait is *inside the actuator*. These 2-wire auto-return valves charge their internal return reserve on power-up **before** driving the motor open — ~5 s of silence, then ~2 s of travel. Closing is fast (≈2.3 s) because the stored charge dumps straight into the motor; the open/close asymmetry over the same 90° stroke is the tell. No software change can shorten it, and the FSM already budgets for it (manual-valve detector constants, lower timeout). Only a hardware swap (3/5-wire valve — loses the power-loss-seals fail-safe — or a true spring-return actuator) would remove the delay.

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
- **Y-axis (Pitch) = published, not used for control.** `Arm Pitch Starboard` / `Arm Pitch Port` are the raw Y angles. Pitch should not change as the lift swings; a later mount check will compare them to the band these sensors record. The pitch-based *list* proxy (`Lift Tilt`, `Lift Tilt Critical`, LEVEL ref capture) stays **retired** (2026-07-24) — the two-IMU **Level Error** is the list signal, and the level-divergence hard stop (§16.2) is the mechanical backstop. Pitch does not clear `angle_trusted` yet (§13).

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

Four **go-to setpoints** the lift can be sent to (ADR-016 Guide is web/HA, no dock LED):

| Mode | Button (LED) | Position | Setpoint |
|---|---|---|---|
| **Lift** | White | Everyday stored position | load-selected shutoff: **boat ceiling** or **empty ceiling**, each approached to `Lift Target %` |
| **Ready** | Orange | Almost down — on the bunks, sealed, walkable | `cal_ready` (captured angle) |
| **Guide** | Web / HA (no dock LED) | Boat **floating**, bunks still a slip centerline | `cal_guide` — sealed rest, like Ready |
| **Lowered** | Green | All the way down (keep venting) | `cal_lowered` (~0 %) |

Under **Advanced → Service & bench**, **Target Height** (bunk inches, same waterline zero as Bunk Height) and **Go to Height** run one existing go-to. Inches convert through the arm sine, because a degree near horizontal buys more height than a degree at the bottom. It is not a parked mode and not an everyday control: auto-maintain still holds only Lift and Ready, and a target that lands in the Lowered band still follows the vent rule.

Plus red **Stop** (§6.3): cancels motion and commands safe outputs in every operating state. Height % is derived linearly between `cal_lift` (100 % — max with the heavy boat) and `cal_lowered` (0 %); Ready and Guide are their own angles; `cal_max` is the empty-lift ceiling, not a mode. **Naming:** buttons/UI say *Lift / Ready / Lower*; firmware internal calibration ids remain `up` / `float` / `down` / `max` / `guide` (`float` = Ready, never Guide).

The **current resting mode** is derived from the live angle vs each setpoint (within the Zone Tolerance band, §6.2): *Lifted / Ready / Guide / Lowered / Between*. Guide is checked **before** Lowered so a captured Guide that sits inside the Lowered zone reports Guide and **stays sealed** on Stop. Go-to is web/HA/`Lift Command`/panel `GUIDE`; no automatic timer.

### 5.1 Boat-presence classifier + two Lift shutoffs

**Why:** A loaded lift cannot reach the empty-tank ceiling (more weight → lower equilibrium). Targeting the exact captured “as high as today” angle fails on a heavier day (stall / blower cap). Lift is one command with two shutoffs:

| Load | Ceiling (captured angle) | Lift parks at |
|---|---|---|
| Confirmed empty | `cal_max` (empty max) | `Lift Target %` of that span |
| Any boat or unknown | `cal_lift` (max with Cobalt) | `Lift Target %` of the boat span |

`Lift Target %` (default **97**) is the always-reach fraction — stop short of the captured ceiling so weight variation still arrives. Unknown is fail-safe boat.

**How:** presence is classified on **raise cycles only**, from the early climb rate over the first 15 %-points (three bands):

| Band | Rate (this install) | `Boat Load State` | Lift shutoff |
|---|---|---|---|
| Empty | ≥ `Empty Raise Rate Min` (**1.20 %/s**) | Confirmed empty | empty ceiling |
| Light / other | between the two sliders (Crestliner ~**0.69 %/s**) | Confirmed other boat | boat ceiling |
| Heavy / Cobalt | ≤ `Heavy Boat Raise Rate Max` (**0.60 %/s**; Cobalt ~**0.40–0.54 %/s**) | Confirmed Cobalt | boat ceiling |
| Unknown | no clean raise yet | Unknown | boat ceiling |

`Boat Present` stays fail-safe ON for boat-or-unknown. Both the occupancy latch and the heavy/light class persist across reboots and reset to unknown / presumed-heavy wherever the boat could change (LOWERED_VENT, bypass entry).

**Decide en route:** Lift from unknown starts toward the **boat** ceiling. A later empty verdict raises the target to the empty ceiling mid-move. A boat verdict (or a slave-throttle classification abort) keeps the boat ceiling. Manual Ready remains authoritative; only unattended Ready recovery requires a confirmed boat. Leveling interaction unchanged: slave-side throttle mid-window aborts classification.

A `Boat Present` occupancy sensor exposes the latch. Display-only `Bunk Height` reports estimated inches relative to the waterline from arm geometry, anchored to the surveyed true-Lowered datum of −13 in.

---

## 6. Go-to-Position State Machine

Pressing a mode button sends the lift to that setpoint; the controller **chooses the direction automatically**.

### 6.1 States

| State | Blower | Valves | Meaning |
|---|---|---|---|
| **HOLD** | OFF | CLOSED | Resting (at a mode, or between). |
| **RAISING** | ON | OPEN / throttled | Pumping air in; rising toward target. Blower + valves energize **together**. |
| **LOWERING** | OFF | OPEN / throttled | Venting; descending toward target. |
| **LOWERED_VENT** | OFF | **OPEN** | Resting at Lowered with vents left open — the lift keeps settling indefinitely. New commands accepted. **The vent stays open at the true bottom** once ADR-018’s settle gate is open and both sides read Lowered (neither in Guide). A power-up frame does not re-enter. If both valid IMUs then read “not lowered” for 2 s, and this is not an emergency lock, the vents close. Stop latches the vent shut; a latched FAULT still seals. |
| **FAULT** | OFF | CLOSED | Latched safe state + reason. |
| **BYPASS** | OFF | **OPEN** | Manual override (§6.6): valves open, blower off, FSM idle. |
| **EMERG_DESCEND** | OFF | **OPEN** (ganged) | Emergency descent (§16.2, ADR-015): list ≥ `Tilt Critical` with the boat high, or in-move catch-up failed — vent both sides down to Ready; seal there if list has collapsed, else continue to Lowered. Stop = seal now; mode buttons refused. A parked vent that does not level stays HOLD (ADR-019). |

*(Numeric FSM slot 1 is reserved/retired — formerly a valve-opening interlock state.)*

### 6.2 Direction selection (on a mode-button request)

Accepted from **rest** (HOLD / LOWERED_VENT) **or mid-move** (RAISING / LOWERING) — last mode press wins. Red is always the cancel path (→ HOLD with safe outputs). FAULT / BYPASS refuse go-to.

```
target = setpoint(mode)                 # load-selected Lift shutoff / cal_ready / cal_lowered
if  height < target - tol:  raise  ->  RAISING   (blower + valves ON together)
elif height > target + tol: lower  ->  LOWERING  (valves ON, blower off)
else:                       already there -> HOLD (or LOWERED_VENT for Lowered)
```

So **Ready is reachable from above or below**, and a mid-move press may **reverse** direction if the new target is on the other side of the live height. Lift is one-sided: at or above the load-selected shutoff counts as there (it will not lower an empty lift that is already past the boat ceiling).

Same-direction retarget only updates the destination (stall / blower / presence trackers keep running). A reverse (raise↔lower) re-arms the move-scoped timers and, on a fresh raise, the boat-presence classifier.

Position checks use **one captured setpoint per mode + one configurable band**: **`Zone Tolerance (°)`**. The band defines both "already there" and "at mode":
- **Lowered:** one-sided — angle > `cal_lowered` − tol (at the lowered cal *or settled beyond it*; valid for the confirmed sensor sign, §2.2).
- **Ready:** captured angle ± tol.
- **Lift:** height ≥ boat `Lift Target %` − tol (tolerance converted °→% through the calibrated span). Empty parked above that is still Lifted.

### 6.3 Stopping

- **RAISING:** stop when the target zone is reached → HOLD. (Backstops: stall detector and absolute blower cap.)
- **LOWERING:** stop when the target zone is reached → HOLD — **except a Lowered target**, which rests in **LOWERED_VENT**. The `Lower Timeout` timer backstops a descent that never confirms its zone.
- **Stop / red button:** any moving or resting state → HOLD with blower off and valves closed. It also clears a latched FAULT, seals emergency descent, and exits Bypass. At Lowered, Stop latches the vent closed; pressing Lower again clears that latch and resumes continuous venting. At Guide (once captured), Stop stays sealed — the Lowered-zone re-entry rule does not fire. Mode buttons do **not** cancel — they retarget (§6.2).
- **Supervisor:** → FAULT (hard) or **auto-stop when both angle sources are lost** (§6.5).

### 6.4 Preconditions

A position-targeted move requires: **a trusted height source** (§9.4), and **not faulted**.

### 6.5 One IMU down, and manual mode when both are down

Starboard is the primary height source. Port runs height only when starboard is untrusted and port is trusted and calibrated (ADR-017). Either way, a single dead IMU **keeps go-to working** on the survivor: valves ganged, leveling off, `Lift Problem` on, red on the IMU cadence (§8). A move in progress retargets into the surviving sensor’s percent. An in-flight inch target stops, because that percent belonged to the sensor that started it.

Manual recovery is only when **neither** IMU can provide height:

- A position-targeted move in progress is **STOPPED** to HOLD.
- **Lift → manual RAISE jog**, **Lowered → manual LOWER jog**, **Ready and Guide refused**, **Stop always live**.
- Manual jogs run **without position feedback**, bounded only by the **absolute blower cap** (up), **lower-timeout** (down), and **Stop**.
- A returning IMU restores go-to on its own. Pressing red does not invent a height.

### 6.6 Bypass mode (manual override)

Hands-off override that **opens both valves and ensures the blower is off**, then does nothing.

- **Outputs:** valves **OPEN**, blower **OFF**. The lift is **vented** — it will not hold on the pneumatics.
- **Entry:** toggle **Bypass Mode** ON (web/HA), or panel Diagnostics `BYPASS_ON`. The dock red button does **not** enter Bypass; it remains Stop-only.
- **Exit:** short-press Stop/red, or toggle the switch OFF → **HOLD**.
- **LEDs:** the three position rings go off and red flashes. A dark panel with a flashing red button is bypass; every ring dark is power loss.
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

> A valve sitting at fully-closed for ~7 s after an open command is **normal** — the actuator's power-up charge dead time, not a control fault (see §2, "Why opening takes ~7 s"). The `valve_cmd` log tag timestamps each Y1/Y2/Y3 relay edge so command-to-motion time is measurable in any log.

Uses today (**display/diagnostic — not an FSM input yet**):
- Live valve state on Diagnostics, independent of Y2/Y3 commands.
- **Manual-operation detector:** a valve at the **wrong end-stop** once it should have left its old seat (~5 s after a command change), or **mid-travel far past worst-case travel** (15 s; measured ~7 s open / ~2.3 s close), persisting ~2 s → status shows **"Manual valve"**. Fixed constants — the former `Valve Travel Time` slider was retired 2026-07-24 (the end-stops are direct evidence; nothing else consumed it).

Future option (not done): use end-stops as stuck-valve fault triggers.

---

## 8. Button LED Scheme

The boat is the height display. The rings show which commands are available, which one is running, and the failures you cannot see from the dock (ADR-017).

| Ring | On | Flashing | Off |
|---|---|---|---|
| Lift (white), Ready, Lower | Whenever the controller is in normal service | The destination of a person-commanded move, or of an emergency descent, until it finishes. A manual jog flashes Lift or Lower. | Bypass (all three) |
| Stop (red) | While raising or lowering, so Stop is the obvious press | See cadences below. A latched fault, emergency, or bypass flash wins over the solid light. | Healthy and idle |

Red cadences, highest priority first:

| Cadence | Meaning | What you do |
|---|---|---|
| Even flash (~2.5 Hz) | Latched fault, emergency descent (including settled at the bottom with vents open), or bypass | Press red. A fault or bypass clears in one press. An emergency seals on the first press and clears on the second. Then a mode can be tried. |
| 3 s even flash, 10 s dark | One or both IMUs untrusted, and the lift is not raising or lowering | With one IMU alive, modes already work on that sensor and this cadence stays until it recovers. With both dead, Lift and Lower are manual jogs. A raise or lower shows solid red instead. |
| Slow double-blink | Wi-Fi is down, the rows above are clear, and the lift is idle | Nothing at the dock. The lift is fully local. A raise or lower shows solid red instead. |

A keeper top-up does not flash a position button. Guide and an inch target have no dock button, so none of the three flash. Red is still solid for those moves, and for a keeper top-up, because the lift is raising or lowering. In manual mode the white and green rings are the jog controls; Ready stays lit and a press does nothing.

---

## 9. Health & Status Model

### 9.1 What we monitor

- **Angle sensors** — ON = fresh valid `0x53` frames (freshness window ~3 s), per IMU.
- **Water-temp sensor (DS18B20)** — present/reading.
- **Equipment / ESP health** — uptime, WiFi signal, hardware.
- **Lift errors** — stall (raising, no progress), blower absolute-runtime cap, level divergence, valve/position anomalies → FAULT with reason.

### 9.2 Roll-up entities (status trio)

- **`Lift Activity`** *(machine-friendly — what the lift is DOING)*: exactly one of `Idle / Raising / Lowering / Leveling / Fault / Bypass / Emergency Descent`. `LOWERED_VENT` reads as `Idle` unless `emerg_lock` (then `Emergency Descent`).
- **`Lift Position`** *(machine-friendly — WHERE it is)*: exactly one of `Lowered / Ready / Lifted / Between / Unknown`.
- **`Lift Status`** *(human-readable line, feeds the panel `msg=`; shown as the **headline at the top of the web Control group**)* via a priority ladder:
  1. `EMERGENCY — descending to Ready (level failure)` / `… to Lowered` / `EMERGENCY — lowered, vents open (level failure)` (ADR-015)
  2. `FAULT — <reason>`
  3. `BYPASS — valve open, blower off (manual override)`
  4. Moving → `MANUAL raising…` (neither IMU trusted) or `Raising -> Ready` (go-to; **no live % embedded**). A port fallback appends `(port angle)`; a dead port appends `(port IMU offline)`.
  5. `Lowered — vent open` (LOWERED_VENT), with the same IMU suffix when one side is out
  6. `Manual valve` (feedback disagrees with closed command, §7.1)
  7. `Angle sensors OFFLINE - manual control` / `Angle OUT OF RANGE - manual control` / `Angle not trusted - manual control` (neither side can run height)
  8. `Not calibrated`
  9. Resting → `Lowered`, `Ready`, `Lifted`, `Between …`, or `Holding`, plus `— port angle` or `— port IMU offline` when that is the degraded case

Short tokens exist for **exact-match Home Assistant automations**. Embedding a live percentage in status floods the HA recorder.

- **Lift Problem** (binary, `device_class: problem`) — the one Home Assistant problem bit. ON for fault / bypass / emergency descent / emergency-Lowered lock / **either IMU untrusted** / starboard uncalibrated / air-loss / a parked vent that did not level (ADR-019). One dead IMU still allows go-to on the other (ADR-017). Air Loss Alert still names the air-loss reason.
- **Lift In Operation** (binary, `running`) — ON only while a **person-initiated** move runs (any button, panel, web, or HA command), OFF when it settles. Machine-initiated motion — keeper top-ups, ADR-013 emergency descent — deliberately stays OFF; that motion reads in `Lift Activity`. Backed by a `user_cmd_move` flag set in every `request_goto_*` intent and cleared when the keeper starts a top-up (ADR-014).

### 9.2b HA command entity

**`Lift Command`** (select: `— / Lift / Ready / Guide / Lower`) — the declarative control for HA automations and scenes: setting an option fires the same `request_*` intent as the matching button, so every interlock applies. Shows the destination while a user-commanded move runs, rests at `—` (a no-op option) so re-selecting always fires. Guide and Lower stay on this select (and their web buttons) for deliberate remote use.

**`Lift Ready`** (cover, `blind`) — everyday HA raise/lower/stop: raise = Lift, lower = Ready, stop = Stop. Position is Lift Height %; above 95% latches fully raised until height falls below 93%. This display hysteresis absorbs small drift; raw height, motion and maintenance remain unchanged. The latch is not persisted, and untrusted height holds the previous display. A partial set-position is ignored. See ADR-014.

### 9.3 UI tiers (web_server sorting groups)

**1 Control** · **2 Status** · **3 Configuration** · **4 Advanced Tuning** · **5 Diagnostics** · **6 Bench**. Bench binary inputs are `disabled_by_default` in HA.

### 9.4 Angle trust ladder

Each IMU is trusted on its own. Automatic go-to needs one trusted, calibrated side (§6.5):

| Rung | Test | Behaviour |
|---|---|---|
| **OFFLINE** | no fresh frames within `Angle Freshness` (~3 s) | that IMU drops out |
| **OUT OF RANGE** | data fresh, but live angle outside calibrated span ± `Angle Plausibility Margin` (default 10°) | that IMU drops out |
| **SENSOR MOVED** *(Guard B — retired 2026-07-24 with the pitch proxy)* | — | a moved/loosened sensor surfaces as persistent two-IMU Level Error / divergence |
| **TRUSTED** | fresh + plausible | eligible as a height source |

Height uses starboard when it is trusted and calibrated, otherwise port. Go-to stops only when neither side is eligible (§6.5). The slave ladder still gates leveling on its own.

Implemented as `angle_valid` (fresh + inside ±95°) → `angle_trusted` (valid + plausible) for starboard, and the same ladder for port (`angle2_valid` / `angle2_trusted`). `height_ok` is starboard when that side is trusted and calibrated, otherwise port. Loss of **both** mid-move auto-stops to HOLD. Leveling degrades to ganged valves when either side loses trust.

Size the `angle_valid` gate to the *mounted* sensor. With the confirmed mounting (arm swing well inside ±90°, lowered = positive), the gate is **±95°**. Remount → re-check this gate and the one-sided Lowered zone (§6.2).

---

## 10. Calibration & Persistence

- **Five master captures** on the lift: **Lift** (max with boat), **Empty Max**, **Ready**, **Guide**, **Lowered** — each records the current master angle. Lift / Ready / Guide / Lowered are go-to modes (Guide has no dock button); Empty Max is the no-boat Lift ceiling and the upper plausibility bound, not a destination.
- **Five slave captures** at the same physical positions (Lowered / Guide / Ready / Lift / Empty Max). Lowered and Lift calibrate the slave's height %, Ready and Guide record the shared posture, and Empty Max extends the slave plausibility range through the full mechanical stroke (§16.2).
- Each capture is also an **editable number** (Advanced Tuning) so a setpoint can be nudged or restored without re-running the lift. A **Calibration Summary** line shows every zone edge.
- Height % is linear between Lift and Lowered (negative span — lowered is the *high* angle, §2.2).
- Stored in flash (`restore_value: yes`).
- Target moves refused until calibrated; the plausibility window (§9.4) derives from these captures.

---

## 11. Safety Contract (invariants)

- Blower ON **only** in RAISING and not faulted. Blower and valves energize together; brief dead-heading is accepted by design.
- HOLD/FAULT: valves commanded CLOSED, blower OFF. (LOWERED_VENT/BYPASS/EMERG_DESCEND: valves OPEN, blower OFF.)
- **Absolute blower runtime cap** (`Blower Max Runtime`, all modes incl. manual test): the blower can never run longer than this. *Must exceed real full-raise time before live use (OEM auto-off ~15 min).*
- RAISING requires angle progress after a grace window, or FAULT (stall). Stall progress is **sign-aware** and **feed-aware** during leveling throttle.
- **Automatic moves require a trusted height source** (§9.4, ADR-017); loss of both IMUs mid-move **auto-stops** to HOLD. One surviving IMU keeps the move, with valves ganged.
- **Level fail-safe** (§16.2, ADR-015, ADR-019): list past `Tilt Critical` (~3°) → emergency descent if the boat is high, else FAULT + make-safe. A parked vent that does not level closes and sits.
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

- **Ready / Guide band** (review 2026-10-09). Zone Tolerance was set back to 2° (it had been persisted at 3.5°). Ready had been stopping on the near edge, about 1.8 in above its capture; Guide coasted 2–3° after the flag. Still open: one supervised Guide lower at 2°, recorded as flag angle, +30 s, and settled angle, before considering 1.5°. Do not raise the Guide capture by a couple of inches on the old 3.5° band — that approach edge lands on Ready. See [`ready_guide_review_2026-10-09.md`](ready_guide_review_2026-10-09.md).
- **Pitch mount check** (observation started 2026-10-06). `Arm Pitch Starboard` and `Arm Pitch Port` are in Home Assistant with `state_class: measurement`, so hourly min/mean/max survive the recorder purge. They do not affect trust, leveling, or go-to. Finish only after both sides have parked time and a few full strokes:
  1. Read the statistics. Note the quiet band at rest and how far pitch moves across a stroke. A hanging or cocked mount is a large step; dock waves are small and common to both sensors.
  2. Pick a sustained departure from each side's installed pitch.
  3. On that departure, clear that side's existing trusted bit (`angle_trusted` / `angle2_trusted`). ADR-017 already gangs the valves and continues height on the other IMU. Do not add a leveling path that runs on one sensor.
  4. Leave acceleration (`0x51`) and gyro (`0x52`) unparsed unless the pitch band cannot see a rattle. The angle output is the filtered gravity vector; raw accel is only the motion that filter removes.
- Decide manual jog: **latched-with-Stop** (current) vs **hold-to-run / deadman**.
- **Maintain-at-Ready policy** (float-away guard): whether sustained Ready sag should escalate to notify/FAULT.
- Decide whether valve feedback should ever gate the FSM (stuck-valve fault) or stay display-only.

Closed 2026-09-10: **ADR-016** — Guide height (floating + bunks as slip guides). Web/HA/panel go-to; sealed rest; no dock button; no firmware auto-trigger. Stop at Guide stays sealed (does not re-enter LOWERED_VENT).

Closed 2026-09-09: **ADR-015** — correction-effectiveness fail-safe and staged Ready→Lowered emergency descent (amends ADR-013). Trigger is “correction not winning in ~30 s of real air” or live list ≥ `Tilt Critical` (default 3°, OEM 3 in side-to-side); catch-up aloft descends instead of sealing; residual list at Ready continues to Lowered with vents open. Dock-proven 2026-09-10: inject, cal-nudge, and a real one-valve dump all sealed at Ready; rest-level feed succeeded; vent pulse hole led to the winning-pulse change. See [`dock_test_2026-09-10.md`](dock_test_2026-09-10.md).

Closed 2026-07-24: the **emergency descent pattern** shipped as EMERG_DESCEND (§16.2, ADR-013); the descent-rate/seal-failure and re-sag alarms shipped as the **Air Loss Alert** (§15.5); **stall thresholds tightened** from the field-data replay (grace 20 s / timeout later 15 s — worst healthy progress gap was 4 s in loaded/empty/Crestliner raises); **blower cap default 4 min** (loaded full raise 134 s); the ~1–2 s command-to-valve delay is attributed to actuator mechanics (firmware path ≈100 ms — measure with §7.1 feedback timestamps if it recurs).

Field move profiles used for tuning live in `field-data/`.

---

## 14. Failure modes (control-relevant)

### 14.1 Wave-driven asymmetric air loss

Working mechanism:

1. Wave action works air out of **one tank** faster than the other.
2. That corner sinks → the **back of that tank lifts clear of the water**.
3. Water inside runs forward → buoyancy shifts → **more air escapes** (positive feedback).

Firmware role: **maintain height**, **correct list** via the dual-tank level layer, **detect + notify** (Air Loss Alert), and **give up altitude** when list passes `Tilt Critical` or an in-move catch-up fails (ADR-015). A parked vent that does not level closes and sits (ADR-019).

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

**Auto-Maintain Height** and **Auto-Maintain Level** switches **default ON**. There is **no sticky lockout** that disables keepers after N events — an unattended lift must not tip or sag because a counter tripped. Visibility is via **per-visit counters** on `Maintain Observe`.

### 15.1 Where each keeper runs

| Parked position / load | Maintain Height | Maintain Level (at-rest) |
|---|---|---|
| **Lift / any load** | yes (to the load-selected shutoff) | **yes** (feed-low / vent-high pulses) |
| **Ready / confirmed boat** | yes, bounded | no |
| **Ready / empty or unknown** | no | no |
| **Lowered / any load** | never | never |

In-move throttle follows **Maintain Level** during any go-to, not only at Lift.

### 15.2 Height keeper

- **Observer always runs:** filters height, estimates sag rate and wave movement.
- **`Auto-Maintain Height`** (default ON): confirmed sag fires a **real top-up through `do_goto()`**. Arms while idle at Lift (boat or empty shutoff) or load-confirmed Ready. Detector: settle delay → filtered height below `setpoint − deadband` for persist time → min interval between top-ups. Defaults: sag deadband **3 %**, settle 180 s, persist 60 s, min interval 12 min.
- **Ready recovery envelope:** automatic Ready inflation is permitted only while both live and filtered height remain no more than **5 %** below Ready. Below that floor—or without a positive loaded classification—recovery is inhibited and surfaced in `Maintain Observe`; it never guesses that a potentially floating/shifted boat is safe to pick up.
- **Up-only** — never auto-lowers.
- **Priority over rest-level:** if master height is below the sag deadband, any rest-level pulse aborts and height owns the recovery.

### 15.3 No sticky lockout — visit counters

**Runaway protection that remains:**
- Min interval between top-ups
- Absolute blower runtime cap
- Stall detector on raises
- Level fail-safe (list past `Tilt Critical` → emergency descent if boat high). A parked vent that does not level closes and sits (ADR-019)
- Rest-level pulse vents the high side only, for 3× the measured both-valve time to erase that list, counted from the OPEN contact; then the valve closes

**Visibility:** when the lift arrives HOLD at a maintained zone different from the current visit, counters reset. `Maintain Observe` shows visit position, switches, top-up + level counts, sag rate, and arm state.

### 15.4 Notification policy

- **Notify immediately:** Lift Problem on; latched fault; angle offline/out of range; blower-cap or stall; emergency descent / level fail-safe.
- **Notify as warning:** high visit top-up or level counts; unusually negative sag rate.
- **Never auto-correct:** unknown position; untrusted angle; lowered/floating state; list at Ready (height only there).

### 15.5 Air Loss Alert

Every keeper intervention is a symptom: a sealed lift should not descend, and a parked lift should not need frequent correction. The **`Air Loss Alert`** problem binary (Status) makes that pattern visible. It **never actuates** — the keepers keep correcting regardless (ADR-004); this is the "and you should know about it" layer. HA alerts on the binary; `Air Loss Detail` (Diagnostics) and the `airloss` log tag carry the reason. Triggers (Advanced Tuning):

| Trigger | Meaning | Default |
|---|---|---|
| **Sealed drop** | filtered height fell this far in 30 s with both valves commanded closed (60 s settle grace after sealing; an event latches the alert ~30 min) | 2 % |
| **Sag rate** | parked sag rate beyond this, downward (60 s settle after sealing; commanded moves do not count) | 20 %/h |
| **Visit events** | height top-ups + level fixes in one visit reach this count | 4 |

Catches seal failure, a manually opened valve, the §14.2 compression spiral, and slow-leak nights — the failures the firmware can only report, not fix. (Normal sealed seep measured ~5–13 %/h; the spiral runs ~1 %/s.)

### 15.6 Baselines to record at commissioning

- Lift / lower time with and without boat
- Normal top-up duration and overnight visit counts at Lift / Ready
- Idle sag rate, wave peak-to-peak, and level-error range at rest

---

## 16. Dual-tank auto-leveling

Each tank has its own vent/fill valve and inclinometer. The controller keeps the lift level by steering air per side. Rationale: see [`adr.md`](adr.md).

### 16.1 Roles — reference (master) / follower (slave)

- **Master = IMU #1** (GPIO32) is the **reference frame** and the primary height source. Port supplies height only while starboard is untrusted (ADR-017). Zones, go-to targets, and stall progress follow whichever sensor is the height source.
- **Slave = IMU #2** (GPIO14) answers one question: *is my side where the master's side is?* The slave is corrected **to match the master**, never the reverse.
- **Physical side labels:** master = **STARBOARD**, slave = **PORT**. UI entity names use side terms via substitutions; master/slave remain internal role names. **Valve A (Y2) = starboard** and **valve B (Y3) = port** — leveling requires valve A = master's side (swap pin substitutions if plumbing differs).
- Because the frame racks, "level" is compared in **calibrated-% space**: each sensor gets its own Lowered/Ready/Lift captures; `level error = slave_height_% − master_height_%` (positive = slave side HIGH).

### 16.2 Intervention thresholds

- **Intervene** when sides differ by more than the **Level Deadband** (configurable; intent ≈ ≤1° of arm).
- **Give up altitude** when live list reaches **`Tilt Critical`** (default **3°** of arm ≈ HydroHoist’s 3 in side-to-side at Lift), or when an in-move catch-up cannot close. Air cannot fix that. Shared entry `start_emergency_descent` (ADR-015). A parked rest-level vent that does not level does **not** descend (ADR-019). **Not during IMU power-up** (ADR-018): automatic descent waits for 20 s of list under `Tilt Critical`, or 60 s of dual-IMU trust if the list never calms. The 2026-10-08 restore was still at 3.8° when an 8 s grace expired.
  - **Boat high** (above the Ready band): **emergency descent** — both valves open ganged, blower off. Ride to Ready. If list has collapsed under `Tilt Critical` and is not still growing (~2 s look) → **FAULT + seal**. If residual list remains, is still growing, or level trust is lost → **keep venting to Lowered**, then **LOWERED_VENT + `emerg_lock`** (vents stay open, `Lift Problem` ON, mode buttons refused). `Lower Timeout` seals as backstop. **Stop seals immediately** (operator override). Trust loss does not stop the descent.
  - **At/below Ready** (or Ready not calibrated / height unknown): **FAULT + make-safe** (both valves closed).

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

Anti-chatter: level error is EMA-filtered (~3 s); throttle engages above deadband, releases inside deadband − hysteresis, and each decision holds a minimum time. In the last 5% of a raise that hold is skipped, so a correction releases or swaps as soon as the list is inside the release band ([ADR-020](adr.md)). The throttle never closes both valves.

### 16.5 At-rest leveling — at Lift only

While parked at **Lift**, a list is corrected by the at-rest keeper. Ready is **height-only**.

**Direction rule:** **vent the high side.** The blower stays off. If that vent does not bring the list back, the lift is in an error state: valves close and it sits. Feeding the low side is not the next step.

**Guard rails:**

1. Own trigger on **level error** past `Rest Level Trigger %` (default 2.5 %) for `Rest Level Persist` (default 15 s).
2. **Maintain Height first** — if master height is below the sag deadband, abort the rest pulse. A height top-up is a raise, not a level feed.
3. Pulse length is **3×** the both-valve descent time for the list measured when the valve opens. That rate is **0.18 %/s** at the top (2026-10-08, 96.5% → 90.8% in 32 s). A 2.5% list is ~42 s open. The clock starts at the OPEN contact. Still closed after 25 s → close. Exit early at the in-move release (`Level Deadband` − hysteresis). Min ~60 s between pulses.
4. Vent floor: close if master would drop below the Lift band, then sit.
5. Pulse over and the list still past the trigger → close and **hold**. No second pulse until the list itself returns under the trigger. No emergency descent. List ≥ `Tilt Critical` is still §16.2, including during the pulse.
6. Visit **level** counter increments when a pulse starts. The mechanical backstop is §16.2, not a counter.
7. Eligibility: HOLD + at Lift + both IMUs trusted + Auto-Maintain Level ON + not bench/bypass + not holding after a failed vent.
8. FSM stays in HOLD while correcting; `rest_pulse` biases `apply_outputs` (valve only). `Lift Activity` reads **Leveling** while a pulse is active.

### 16.6 FSM interactions

- **Two-sided completion:** Ready and lower-to-a-setpoint end when master is at target **and** |level error| ≤ deadband. A **raise** does the same past the deadband (catch-up, then emergency descent above Ready). Inside the deadband, a raise does not seal until |level error| is inside the release band (`Level Deadband` − hysteresis). That trim feeds the low side, gives the valve 20 s, and seals if the list then stops closing — a leftover inside the deadband sits, it does not descend ([ADR-020](adr.md)). The last 5% of a raise also skips the throttle min-hold, so a correction can swap as soon as the list comes in. Lowered-target moves skip the gate (`LOWERED_VENT` leaves both valves open).
- **Feed-aware stall:** during RAISING with the master valve throttled, progress is tracked on the side being fed.
- **Auto-maintain top-ups** are normal go-to raises; the leveling layer rides along.
- **Manual moves** (angle untrusted): leveling disabled, both valves ganged.

### 16.7 Degraded modes

| Condition | Behaviour |
|---|---|
| Auto-Maintain Level OFF | valves ganged; decisions still logged as `LEVEL(shadow)` |
| Slave stale / implausible | starboard keeps height; leveling + completion gate disabled, valves ganged |
| Master trust lost, port still trusted | move continues on port percent; valves ganged |
| Both IMUs untrusted mid-move | auto move stops |
| Sides diverge past hard stop | emergency descent if boat high; else FAULT + make-safe |
| Parked vent does not level | valves close, sit; no feed, no descent (ADR-019) |
| Slave can't catch up in time | `level_fail_catchup` — descent if boat high; else FAULT + seal |

### 16.8 Calibration (eight captures + level check)

1. True Lowered → capture LOWERED on **both** sensors.
2. Ready (boat floating level) → capture READY on both.
3. Lift (max with the heavy boat), verified level → capture LIFT on both.
4. Empty Max (boat off, tanks at the empty ceiling) → capture EMPTY MAX on both.
5. Verify Level Error ≈ 0 % at each setpoint.

### 16.9 Key entities (leveling)

Switches: `Auto-Maintain Level` / `Auto-Maintain Height` (default ON), bench `Valve Port (Y3)`. Numbers: slave cal angles; Level Deadband / Hysteresis / Min Hold / Catch-up Timeout; Rest Level Trigger / Persist. Sensors: `Arm Angle Port`, `Height Port`, `Level Error (%)`, `Maintain Observe`. Binary: `IMU Port OK`. Text: `Level Status` (simple, state-aware: what leveling is doing, or which side is high/low), `Valve Positions`.

---

## 17. Observe instrumentation (always on)

Diagnostic entities, computed every 250 ms, **never write blower/valve**:

- **Lift Height (filtered)** — wave-stripped EMA (*Maintain Smoothing*).
- **Lift Wave P-P (60 s)** — peak-to-peak of raw height over 60 s.
- **Lift Sag Rate (%/h)** — slope of filtered height.
- **Maintain Observe** — parked position, per-visit top-up / level-fix counts, sag rate.

Tunable defaults (Advanced Tuning): height **4 % / 60 s / 12 min / 180 s / 20 s**; rest-level **3 % / 30 s**; Tilt Critical **3°** (never-exceed / Ready residual; OEM 3 in side-to-side at Lift).

---

## 18. Panel link (RS485)

A wired RS485 link (GPIO13 TX / GPIO16 RX, **9600 8N1**, auto-direction transceiver) connects the lift to the optional dock touch panel:

- **STA heartbeat** (~2 Hz, lift → panel): state token + height % + problem/trust flags + water temp + human status line.
- **CMD parser** (panel → lift): `req=` tokens map onto the **same `request_*` intent scripts** a dock button uses — the panel cannot bypass the FSM or safety supervisor.

Protocol: [`boat_lift_link_protocol.md`](boat_lift_link_protocol.md). Panel UI: [`boat_lift_panel_design_revA.md`](boat_lift_panel_design_revA.md).

---

*Design decisions and rationale: [`adr.md`](adr.md).*
