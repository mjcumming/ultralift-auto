# Architecture Decision Record — UltraLift Auto

Short record of accepted design decisions for the dual-tank HydroHoist UltraLift automation in this repo. The as-built behaviour lives in [`boat_lift_design.md`](boat_lift_design.md); this file is the *why*.

Status values: **Accepted**.

---

## ADR-001 — Master / slave IMU roles with starboard / port labels

**Decision:** IMU #1 (GPIO32) is the **master** reference for all height, zones, go-to targets, and angle trust. IMU #2 (GPIO14) is the **slave** and is corrected to match the master. UI labels use **starboard** (master) and **port** (slave). Valve A (Y2) must be the master’s tank.

**Rationale:** Height needs a single authority; comparing two absolute angles on a racked frame is noisy. Calibrating each sensor at the same physical setpoints and comparing in %-space removes racking. Side words match dock language; master/slave stay as internal roles.

**Consequence:** Leveling never moves the master to the slave. If plumbing is reversed, swap pin substitutions — do not invert the control rule in code. Height authority when starboard is untrusted is amended by ADR-017.

---

## ADR-002 — Leveling is a bias layer, not a second FSM

**Decision:** Keep the go-to FSM (HOLD / RAISING / LOWERING / …). With Maintain Level on, `apply_outputs` opens/closes individual valves to **throttle the side that is ahead**. Rest-level pulses at Lift stay in HOLD and bias outputs via `rest_pulse`.

**Rationale:** One blower can only feed one path at a time. A parallel “level FSM” would fight go-to and maintain. Biasing which valve is open reuses every existing safety backstop.

**Consequence:** Stall detection must be feed-aware. Move completion for Ready/Lift waits for both master-at-target and level-within-deadband.

---

## ADR-003 — Maintain Height vs Maintain Level eligibility

**Decision:**

| Position / load | Maintain Height | Maintain Level (at rest) |
|---|---|---|
| Lift / any load | yes (to the load-selected shutoff) | yes |
| Ready / confirmed boat | yes, only from 3 % low through the 5 % safe floor | no |
| Ready / empty or unknown | no | no |
| Lowered / any load | no | no |

In-move throttle follows Maintain Level on every go-to. Both switches **default ON**.

**Rationale:** Everyday storage (Lift) needs height and level. Ready is a boat-supported posture: a positively classified load may receive height-only recovery inside a narrow envelope, but below the safe floor the boat may be floating or shifted, so automatic inflation is inhibited. An empty lift cannot reliably hold Ready. Lowered means the boat is floating.

**Consequence:** Level monitoring and the divergence hard stop remain live everywhere trusted, while parked level pulses remain Lift-only. Manual Ready requests always select Ready; load classification limits only unattended Ready recovery, and `Maintain Observe` reports any recovery inhibition.

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

## ADR-006 — Boat presence from raise rate; two Lift shutoffs

**Decision:** Infer presence from early climb rate on raises (loaded is much slower than empty). Unknown = boat ON. **Lift is one command** with two shutoffs: confirmed empty → empty-max ceiling; any boat or unknown → boat (Cobalt) ceiling. Each ceiling is approached to `Lift Target %` so a heavier day still arrives. A Lift from unknown starts at the boat ceiling and **raises the target mid-move** if the verdict is empty.

**Rationale:** A loaded lift cannot reach the empty-tank equilibrium, and boat weight varies (fuel, gear). A separate Lift Max mode is unnecessary once the beam/roof constraint is gone: every boat parks at the Cobalt height; only an empty lift uses the higher stored empty-max. Fail-safe polarity prefers the lower (boat) shutoff.

**Consequence:** Tune `Empty Raise Rate Min` from local empty vs loaded profiles. Slave-side throttle during the timing window aborts classification for that raise (boat shutoff holds). There is no Lift Max destination; `cal_max` remains the empty ceiling and the upper plausibility bound. Supersedes the earlier roof-guard form of this ADR.

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

**Consequence:** "Sensor moved" Guard B is retired with it; the trust ladder relies on freshness + plausibility + the level hard stop. As of 2026-10-06 both raw pitches are published again as diagnostic sensors so the installed band can be measured. That does not restore the list proxy, and pitch still does not affect trust or leveling.

---

## ADR-013 — Emergency descent on level divergence with the boat high

**Amended by [ADR-015](#adr-015).** The descent maneuver (ganged vent, blower off, while the boat is high) stands. Trigger set, 3° never-exceed, catch-up aloft, and the staged Ready→Lowered ending are in ADR-015.

**Decision:** When the two sides diverge past `Tilt Critical` **and the boat is above the Ready band**, do not seal in place. Enter **EMERG_DESCEND**: both valves open ganged, blower off, ride down to the Ready band (or below it, or `Lower Timeout`), then seal into a latched FAULT. Stop seals immediately (operator override); mode buttons are refused; trust loss does not stop the descent — the timeout still seals. At or below Ready (or without a Ready calibration or known height), the response stays FAULT + make-safe. (Original trigger was only the divergence hard stop at ~5° — `level_fail_catchup` and stall sealed in place.)

**Rationale:** For the catastrophic asymmetric failure (hose off a tank), sealing holds the *good* side aloft while the failed side falls — the controller would actively maximize the twist with the boat high. Opening both valves vents the high side down toward the failed side: the twist shrinks during the descent and the water progressively takes the boat's weight. This extends the system's existing philosophy — power loss drifts down to float, the bottom is the resting truth — to "when leveling has provably failed, give up altitude." A list at Ready is harmless; a list aloft can put the boat off its bunks.

**Consequence:** The controller can initiate motion nobody requested, on a possibly unattended boat. Accepted: the 5° trigger means something is already mechanically wrong, the alternative (twisting aloft) is strictly worse, and power loss already produces an uncontrolled version of the same descent. Panel/HA show `EMERG_DESCEND` / `Emergency Descent`; `Lift Problem` is ON throughout.

## ADR-014 — Home Assistant control surface: buttons + command select + Lift/Ready cover

**Decision:** Home Assistant drives the lift through the same four stateless buttons as the web UI, plus a **`Lift Command` select** (`— / Lift / Ready / Lower`) whose set-action fires the matching `request_*` intent, plus a **`Lift Ready` cover** whose open/close/stop map to Lift / Ready / Stop only. A **`Lift In Operation`** binary (`running`) is ON only while a **person-initiated** move runs: a `user_cmd_move` flag is set by every `request_goto_*` intent (dock buttons, panel, web, HA — all human paths) and cleared when the auto-maintain keeper starts a top-up; emergency descent is excluded outright. The select mirrors the destination of a user-commanded move and rests at `—` (selecting `—` is a no-op), so re-selecting the same destination always fires.

The cover is a **blind** (`device_class: blind`, no tilt) so voice assistants use raise and lower, not garage-door open/close. Its position is **Lift Height %** (0 = Lowered calibration, 100 = Lift calibration); above 95% it latches fully open and remains there until height falls below 93% (October 6, 2026 refinement). This display-only hysteresis absorbs drift around 95%; raw height and motion/maintenance thresholds are unchanged. Unknown or non-finite height retains the previous display, and the volatile latch is requalified from live feedback after boot. Raise/open goes to Lift, lower/close goes to Ready, stop is Stop. A partial `set_cover_position` is ignored, so a slider cannot send the boat to an arbitrary height. It does not restore-and-call on boot. **Full Lower stays on `Lift Command` and the Lower button** — intentional remote use (phone on the walk to the dock), not an everyday dashboard action.

**Rationale:** "The lift is in operation" means *someone asked it to do something* — keeper top-ups and the ADR-013 descent are housekeeping and emergency response, and lighting a "running" flag for them would train people to ignore it (motion is still visible in `Lift Activity`). An unbounded cover was rejected: covers get swept into bulk actions ("close all covers", good-night scenes, voice assistants exposing a garage door), and open/close/position-% semantics collapse Ready, LOWERED_VENT, and the load-selected Lift shutoffs into an ambiguous slider. A Lift↔Ready-only cover keeps the dashboard/voice envelope on the bunks (Ready is still boat-supported and sealed). Lower remains available as an explicit select/button so a walk-up launch is still one phone tap, not one "close all covers" away.

**Consequence:** Everyday HA automations and dashboards use the cover (`cover.open` / `cover.close` / `cover.stop`) or `select.select_option` Lift/Ready. Lower is still on the select and the Lower button. Do not add the cover to a generic "all covers" scene. The select stays HA-furniture at the bottom of Advanced on the web page; the cover files in Control after the four buttons.

---

## ADR-015 — Correction-effectiveness fail-safe; staged emergency descent

**Decision:** Treat a slow leak as *correct then notify*, and a leak the correction cannot win as *give up altitude* (ADR-013 maneuver). Detect and feed-low stay as they are. Fail the correction if either:

- live list ≥ **`Tilt Critical`** (default **3°** of arm — HydroHoist UL2 “stop if more than 3 inches side-to-side”; on this 51 in arm that is ~3° at Lift), or
- a rest-level pulse has had **~30 s of real air** (clock starts when the feed valve’s OPEN contact is true, else after 8 s actuator grace) and the list is **not shrinking**. Not-yet-level while the list **is** shrinking is not a fail and **not** a reason to stop: keep correcting until the same release as in-move (`Level Deadband` − hysteresis) or the vent height floor. The air clock is only a not-winning detector.

Above Ready, that fail (and the 3° never-exceed, and in-move **`level_fail_catchup`**) enter **EMERG_DESCEND**. At/below Ready they still **FAULT + seal**. Stall still seals. Air Loss Alert stays notify-only.

Descent is staged: ride to Ready with both valves open, blower off. If list has collapsed under `Tilt Critical` and is not still growing (~2 s look), **FAULT + seal**. If list is still past `Tilt Critical`, still growing, or level trust is lost so we cannot confirm the bunks have the boat, **keep venting to Lowered**. Arrival at Lowered goes to **LOWERED_VENT** with **`emerg_lock`**: vents stay open (ADR-009), `Lift Problem` ON, mode buttons refused, Stop acknowledges into FAULT + seal. `Lower Timeout` still seals as backstop.

**Rationale:** You cannot measure a clean leak rate while correcting, and you should not sit idle to measure one. A 5° wall is already past the OEM 3-inch operating limit (~2.7 in of bunk step at Lift). Waiting for it while rest-level pulses abort and retry winds the torsion bars. Sealing a failed catch-up aloft is the same “hold the good side up” mistake as sealing a hose-off. Ready is the right first rest; full Lower is only if Ready did not unload the tanks.

**Consequence:** `Tilt Critical` default 5° → 3°. The entity is flash-persisted — OTA does not move a live device; nudge the slider to 3 after flash. Panel token stays `EMERG_DESCEND`. Status names Ready vs Lowered vs “lowered, vents open.” No new timeout slider (30 s is a safety budget, not a dock tweak).

---

## ADR-016 — Guide: floating + bunks as slip centerline

**Decision:** Add a fourth calibrated height, **Guide**: the boat is **floating**, but the bunks are still high enough to **center the hull in the slip** while docking. Go-to from the web **Guide** button, `Lift Command` → Guide, and panel `req=GUIDE`. Arrival is **HOLD + sealed** (like Ready), never `LOWERED_VENT`. No physical dock button or LED. No firmware auto-trigger (leave-then-raise-later is an HA / human decision).

**Name:** **Guide** — short, says what the bunks do. Rejected or deferred:

| Name | Why not (this pass) |
|---|---|
| **Guide** | Chosen. Bunks guide the hull. Internal id `guide`. |
| Slip | Fine everyday word; sounds like “the boat slipped off.” |
| Dock / Docking | Overloaded with “the dock” and the dock buttons. |
| Align / Center | Accurate, but less about the bunks. |
| Guided entry | What it is, too long for Status / panel `st=`. |
| Float | **Do not use.** `cal_angle_float` is already Ready. |

**Rationale:** Dock 2026-09-10 showed three different jobs at the bottom of the stroke that today’s three modes cannot share:

- **Ready** — boat *on* the bunks, sealed, walkable (~13% / ~46° after recapture).
- **The useful docking height** — boat floating, bunks still a centerline fence (~1–5% / ~51–53° that morning).
- **Lowered** — keep venting (ADR-009). Entering the Lowered zone from HOLD re-opens both valves. That is why the “pretty good” float ran away toward the 53.8° bottom.

Stealing Ready would give up the walkable seal. Stealing Lowered would break continuous vent at the true bottom. The new zone will overlap Lowered’s one-sided band (`cal_lowered − zone_tol`), so Guide **must win**: report Guide before Lowered; Stop at Guide stays sealed (`HOLD` + `pos_at_guide` must not re-enter `LOWERED_VENT`). No fifth LED. Dock rings no longer show which zone the lift is in (ADR-017).

**Consequence:** Empty `cal_guide_done` / `cal2_guide_done` until the operator Stop-seals at the intended height and presses **Calibrate: set GUIDE** (both sides). Do not invent an angle from a moving dump. HA `number.set_value` on template cals often does not publish — use the capture buttons while sitting still. Panel `st=GUIDE` when HOLD and `pos_at_guide`. Go-to refuses until captured. Cover stays Lift↔Ready only. Do not add a firmware “after N minutes at Lowered, raise to Guide” — that guess is often wrong (boat still there, want to stay down, extra draft); HA can fire Guide when you actually decide. The dock LED no longer encodes “at Guide” (ADR-017); Guide still wins over Lowered in the reported position.

---

## ADR-017 — Either IMU can run height; dock LEDs show commands and faults

**Decision:** Starboard remains the primary height source. If starboard is untrusted and port is trusted and calibrated, go-to, zones, stall progress, and height maintenance use **port’s** captures. If only port is untrusted, starboard keeps height. Leveling and the level hard stop still require **both**. One dead IMU gangs the valves and leaves `Lift Problem` on. Both dead drops to manual jog (Lift raises, Lower lowers, Ready and Guide refuse) until a sensor returns.

Dock rings:

| Ring | Healthy idle | Move | Fault / emergency / bypass | One or both IMUs untrusted | Wi-Fi down only |
|---|---|---|---|---|---|
| White, Ready, Lower | On | The commanded destination slow-flashes; the other two stay on. Emergency flashes its destination. | On, except **bypass turns all three off** | On (manual jog flashes Lift or Lower) | On |
| Red | Off | Off | Even flash. Press red. | 3 s of even flash, then 10 s dark | Slow double-blink |

A person-commanded move flashes its button. A keeper top-up does not. Guide and an inch target have no dock button, so none of the three flash. Red priority is latch, then IMU, then Wi-Fi.

Pressing red still clears a latched fault (one press), an emergency (first press seals and latches, second clears), and bypass (one press). That returns to holding so a mode can be tried. It does not clear a live IMU or Wi-Fi indication; with one IMU alive the modes already work.

**Rationale:** The boat is the height display at the dock. The rings need to show which command is running and which failures you cannot see: a latched stop, a dead sensor, and no Wi-Fi. Both tanks already have Lift / Ready / Guide / Lowered / Empty Max captures from the same physical positions, so port percent is a real height, not a guess. Stopping the lift because the primary IMU died, while the secondary is still reading, gives up a move the frame can still finish. Leveling on one IMU would invent a list.

**Consequence:** Amends ADR-001’s “master is the sole height authority.” ADR-016’s “green Lowered LED off at Guide” is retired; Guide still wins in `Lift Position` and status text. An inch go-to that is in flight when the height source switches stops, because that percent belongs to the sensor that started it. Named modes retarget into the new sensor’s percent. Requires OTA.
