# Architecture Decision Record — UltraLift Auto

Short record of accepted design decisions for the dual-tank HydroHoist UltraLift automation in this repo. The as-built behaviour lives in [`boat_lift_design.md`](boat_lift_design.md); this file is the *why*.

Status values: **Accepted**.

---

## ADR-001 — Master / slave IMU roles with starboard / port labels

**Decision:** IMU #1 (GPIO32) is the **master** reference for all height, zones, go-to targets, and angle trust. IMU #2 (GPIO14) is the **slave** and is corrected to match the master. UI labels use **starboard** (master) and **port** (slave). Valve A (Y2) must be the master’s tank.

**Rationale:** Height needs a single authority; comparing two absolute angles on a racked frame is noisy. Calibrating each sensor at the same physical setpoints and comparing in %-space removes racking. Side words match dock language; master/slave stay as internal roles.

**Consequence:** Leveling never moves the master to the slave. If plumbing is reversed, swap pin substitutions — do not invert the control rule in code.

---

## ADR-002 — Leveling is a bias layer, not a second FSM

**Decision:** Keep the go-to FSM (HOLD / RAISING / LOWERING / …). With Maintain Level on, `apply_outputs` opens/closes individual valves to **throttle the side that is ahead**. Rest-level pulses at Lift stay in HOLD and bias outputs via `rest_pulse`.

**Rationale:** One blower can only feed one path at a time. A parallel “level FSM” would fight go-to and maintain. Biasing which valve is open reuses every existing safety backstop.

**Consequence:** Stall detection must be feed-aware. Move completion for Ready/Lift/Max waits for both master-at-target and level-within-deadband.

---

## ADR-003 — Maintain Height vs Maintain Level eligibility

**Decision:**

| Position | Maintain Height | Maintain Level (at rest) |
|---|---|---|
| Lift | yes | yes |
| Ready | yes | no |
| Lift Max | yes | no |
| Lowered | no | no |

In-move throttle follows Maintain Level on every go-to. Both switches **default ON**.

**Rationale:** Everyday storage (Lift) needs height and level. Ready is a float-away risk if height sags, but at-rest level pulses there are lower priority than getting height back. Lift Max is a roof-clearance / storage extreme — height only. Lowered means the boat is floating.

**Consequence:** List at Ready or Max is visible in Level Error / Observe but not auto-pulsed at rest; height top-ups still run where allowed.

---

## ADR-004 — No sticky maintain lockout

**Decision:** Do not disable height or level keepers after N events. Use per-visit counters on `Maintain Observe` plus existing backstops (min interval, blower cap, stall, level-divergence hard stop, pulse time caps).

**Rationale:** A keeper that silently stops is worse than a noisy blower. An unattended lift can tip or sag if correction is locked out.

**Consequence:** Operators watch visit counters and sag rate for leak severity; hard FAULT remains the stop for mechanical divergence.

---

## ADR-005 — Blower on fail-safe NO path

**Decision:** Drive the blower contactor/relay on the **normally open** path so coil de-energized = motor OFF (cold boot, dead controller, and make-safe all leave the blower off).

**Rationale:** The opposite polarity is fail-deadly: a dead or freshly booted controller can blip or leave the motor on.

**Consequence:** Output polarity (`inverted` / wiring) must be verified at commissioning on every install.

---

## ADR-006 — Boat presence from raise rate; Lift Max roof guard

**Decision:** Infer presence from early climb rate on raises (loaded is much slower than empty). Unknown = boat ON. Boat ON blocks Lift Max. From a low start with unknown presence, allow Max to start and **demote to Lift** if the verdict is not EMPTY (“decide en route”).

**Rationale:** No reliable presence switch on many installs. Lift Max can collide with a boathouse roof if a boat is aboard. Fail-safe polarity prefers a blocked Max over a roof strike.

**Consequence:** Tune `Empty Raise Rate Min` from local empty vs loaded profiles. Slave-side throttle during the timing window aborts classification for that raise.

---

## ADR-007 — Sensor axis and sign convention

**Decision:** Arm **roll (X)** is height. Fully **lowered = positive** angle; raise drives the angle **negative**. Height % maps the span; Lowered zones are one-sided for that sign. Pitch (Y) is observe-only for “sensor moved.”

**Rationale:** Matches the field mounting on the parallelogram arm. Wrong sign breaks zones, stall progress, and presence rates.

**Consequence:** Remounting a sensor requires re-validating sign, the ±95° validity gate, and one-sided Lowered logic.

---

## ADR-008 — Blower and valves commanded together

**Decision:** On raise, energize blower and open valves simultaneously. No “wait for valve fully open” interlock state.

**Rationale:** Open-loop valve travel is seconds; sequencing delayed response without a strong safety gain. Brief dead-heading is acceptable for this blower/valve set.

**Consequence:** FSM slot formerly used for valve-opening interlock stays reserved/unused. Valve feedback remains diagnostic unless later used for stuck-valve faults.

---

## ADR-009 — LOWERED_VENT resting state

**Decision:** Reaching Lowered leaves both valves **open** indefinitely so the lift keeps settling — **the vent stays open at the bottom, always**: a standing supervisor rule re-enters LOWERED_VENT from any HOLD settled in the Lowered zone (post-boot, post-Stop, post-timeout), so Stop at the bottom is a no-op. A latched FAULT still seals.

**Rationale:** “All the way down” is a physical end, not a precise angle band; leaving the vent open matches how the OEM system is used when floating the boat.

**Consequence:** Maintain Height does not arm at Lowered. Presence resets to presumed-ON in LOWERED_VENT.

---

## ADR-010 — Panel is request/display only

**Decision:** The optional RS485 touch panel emits the same validated intents as dock buttons and renders lift-reported status. It never drives outputs and is never a control authority. HA is observability / environment only.

**Rationale:** A boat lift is a crush hazard; safety must survive network, HA, and panel failure. Physical Stop on the dock remains the human backstop.

**Consequence:** Link loss disables panel controls; the A16 continues from local buttons. Protocol: [`boat_lift_link_protocol.md`](boat_lift_link_protocol.md).

---

## ADR-011 — Public device naming

**Decision:** Repo config and ESPHome device name are generic: `boat-lift.yaml`, `device_name: boat-lift`, `friendly_name: Boat Lift`, project `ultralift.boat_lift`.

**Rationale:** This archive is a reference build, not a site- or boat-specific product name.

**Consequence:** Changing `device_name` from a prior install name creates a **new** Home Assistant device; entity history and stored calibrations under the old name do not carry over automatically — re-capture or copy number entities after first flash under the new name.

---

## ADR-012 — Retire the pitch tilt proxy

**Decision:** Remove the pitch-based tilt proxy: the `Lift Tilt` sensor, `Lift Tilt Critical` annunciator, the LEVEL ref capture button, and the persisted pitch reference. Pitch is still decoded but unused. `Tilt Critical (deg)` remains — it is the level-divergence hard-stop threshold (§16.3), and its entity name is kept so the flash-persisted value survives.

**Rationale:** The proxy predates the second IMU. The two-IMU Level Error measures list directly in calibrated %-space, and a moved or loosened sensor surfaces as persistent level error or divergence. The pitch proxy was unproven and deliberately excluded from Lift Problem — a status entity nobody may act on is clutter, not safety.

**Consequence:** "Sensor moved" Guard B is retired with it; the trust ladder relies on freshness + plausibility + the level hard stop.

---

## ADR-013 — Emergency descent on level divergence with the boat high

**Decision:** When the two sides diverge past `Tilt Critical` (~5°) **and the boat is above the Ready band**, do not seal in place. Enter **EMERG_DESCEND**: both valves open ganged, blower off, ride down to the Ready band (or below it, or `Lower Timeout`), then seal into a latched FAULT. Stop seals immediately (operator override); mode buttons are refused; trust loss does not stop the descent — the timeout still seals. At or below Ready (or without a Ready calibration or known height), the response stays FAULT + make-safe. The trigger is deliberately **only** the divergence hard stop — `level_fail_catchup` and stall keep sealing in place.

**Rationale:** For the catastrophic asymmetric failure (hose off a tank), sealing holds the *good* side aloft while the failed side falls — the controller would actively maximize the twist with the boat high. Opening both valves vents the high side down toward the failed side: the twist shrinks during the descent and the water progressively takes the boat's weight. This extends the system's existing philosophy — power loss drifts down to float, the bottom is the resting truth — to "when leveling has provably failed, give up altitude." A list at Ready is harmless; a list aloft can put the boat off its bunks.

**Consequence:** The controller can initiate motion nobody requested, on a possibly unattended boat. Accepted: the 5° trigger means something is already mechanically wrong, the alternative (twisting aloft) is strictly worse, and power loss already produces an uncontrolled version of the same descent. Panel/HA show `EMERG_DESCEND` / `Emergency Descent`; `Lift Problem` is ON throughout.
