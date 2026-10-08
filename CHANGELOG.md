# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **Arm Pitch Starboard / Arm Pitch Port.** Raw Y from each IMU, diagnostic only, recorded in Home Assistant (`state_class: measurement`) so a later mount check can be sized from real parked time and full strokes. Nothing in the controller reads them. Requires OTA. Follow-up is in the design doc §13.
- **Target bunk height and Go.** Under Advanced → Service & bench: a waterline height in inches (same zero as Bunk Height) and Go runs one existing go-to. Inches convert through the arm sine, so a degree near horizontal is not treated as the same height change as a degree at the bottom. Not on the everyday page, and not a parked mode: Guide, auto-maintain, and the Lowered vent rule are unchanged. Requires OTA.
- Added a bundled local dashboard with position commands, sticky Stop, lift/level/valve status and automatic maintenance controls. Calibration, tuning and service/bench controls are collapsed under Advanced; diagnostics is read-only and collapsed. Existing entity IDs, calibration storage, defaults, motion logic, dock controls and panel behavior are retained.
- Added a local simulation fixture for browser checks; the fixture never connects to a device. See `docs/web_dashboard.md`.

- **Guide height** ([ADR-016](docs/adr.md)): “boat floating, bunks still centering the hull in the slip.” Web **Guide** button, `Lift Command` → Guide, panel `req=GUIDE`. Arrival is HOLD + sealed (not `LOWERED_VENT`). Status / Position / panel `st=GUIDE` in-zone. Stop stays sealed. No dock button or LED, no firmware auto-trigger. Capture with **Calibrate: set GUIDE** on both sides only when Stop-sealed at the intended height — go-to refuses until then. Requires OTA.
- **`Test: inject level fail`** (Bench): one-shot switch that drives the real level hard-stop path into emergency descent. ALWAYS_OFF on boot; clears itself when the descent starts. For proving ADR-015 without faking IMU cal or winding the frame. **Moves the lift.** Requires OTA.
- **`Lift Ready` cover for Home Assistant.** A blind: raise = Lift, lower = Ready, Stop = Stop. Position is Lift Height %; above 95% reports fully raised. A partial set-position is ignored, so the boat cannot be sent to an arbitrary height. Full Lower stays on `Lift Command` and the Lower button. Requires OTA. Amends [ADR-014](docs/adr.md).

### Fixed

- **Power-up must not descend.** On 2026-10-08 a power restore at Lift opened both vents (`at lowered - vent reopened`) and then emergency-descended (`level_divergence`) while height was still ~98–105%. The 8 s hard-stop grace was already over — the IMU slew was still 3.8° at 11 s — and the lowered-vent re-entry had no grace at all, so one early frame could latch the valves open. Automatic vent (lowered re-entry and rest-level) now waits until both IMUs have held list under Tilt Critical for 20 s. The hard stop waits for that same calm window, or 60 s of dual-IMU trust if the list never calms, and then a real divergence still trips on the next tick. Re-entry also requires both sides in Lowered and neither in Guide. A startup `LOWERED_VENT` whose lowered reading disappears is sealed back to HOLD. Person commands are unchanged. [ADR-018](docs/adr.md). Requires OTA.

- **Stable fully raised cover display.** `Lift Ready` latches 100% above 95% height and releases only below 93%, so small drift around 95% does not chatter. Raw height, motion, calibration and automatic maintenance are unchanged. Invalid feedback holds the previous display; the latch is rebuilt from live height after boot.

- **Emergency descent no longer times out on the same tick it starts.** `start_emergency_descent` stamps `lower_start_time` with `millis()` after the supervisor already sampled `now`; unsigned wrap looked like a 15 min timeout and sealed at Lift with `descent timeout` (inject test 2026-09-10, boat never moved). Timeout now requires `now >= start`. Requires OTA.
- **Air Loss no longer trips on a finished lower.** Sag rate is measured only while HOLD; a commanded descent is discarded, and the sag-rate trigger shares the sealed-drop 60 s settle grace. A Ready arrival (2026-09-06) had been alerting `sag rate high` for ~3 min after a clean 149 s go-to. Requires OTA.
- **Power-up must not open the valves.** A cold boot (or power restore) could publish one plausible-but-wrong IMU frame before both sensors settled; the level EMA then crossed Tilt Critical in under a second and ADR-013 emergency-descended — both valves open, boat high. Stop during that descent latched `FAULT: level_divergence - operator stop`. The hard stop now waits for 8 s of continuous dual-IMU trust before it can vent or fault, so a power cycle stays HOLD / valves closed. A real divergence hours later still trips immediately. Requires OTA.

### Changed

- **Dock LEDs show the command and the failures you cannot see** ([ADR-017](docs/adr.md)). White, Ready, and Lower stay on. The destination you asked for slow-flashes until the move ends; a keeper top-up does not. Red stays dark while the lift is healthy. An even red flash is a latched fault, an emergency, or bypass (bypass also turns the three position rings off) — press red until it clears, then try a mode. One or both IMUs untrusted uses a 3 s flash / 10 s dark pattern. Wi-Fi down, with nothing else wrong, is a slow double-blink. Requires OTA.
- **Either IMU can run height** ([ADR-017](docs/adr.md), amends ADR-001). Starboard stays primary. If it drops out and port is calibrated and trusted, go-to continues on the port captures with the valves ganged and `Lift Problem` on. Port dropping out leaves starboard in charge the same way. Manual jog is only when both are out. Leveling still needs both. Requires OTA.
- **Simpler dashboard status.** Water temperature is prominent beside lift status. Level shows one status plus detail; the read-only Operations card lists blower On/Off and each valve's end-stop state. Detailed per-side readings remain in Diagnostics.

- **Parked level pulse no longer stops while it is winning.** The 10 s vent / 30 s feed clocks are only a *not-shrinking* fail-safe. A winning pulse keeps going until the same release as a raise (`Level Deadband` − hysteresis). Dock 2026-09-10: a 10 s vent ended at 2.7% in the 2–3% hole and never retried. Requires OTA.
- **Rest-level defaults tightened** (live sliders set 2026-09-10; yaml initials match for new flashes): Trigger **3% → 2.5%**, Persist **30 s → 15 s**, in-move Deadband **2% → 1.5%** (~0.9°). Hysteresis stays 1%. Tilt Critical stays 3°. Live device already has the slider values; OTA is only for the winning-pulse change.
- **`Tilt Critical` slider is 1–5°** (was 1–30). 3° remains the operating default; 1° is only a test trip. Past 5° is dump/slip territory, not a setpoint. Requires OTA for the new bounds (the live value you already set to 3 is unchanged).
- **Level fail-safe is correction-effectiveness, not a 5° wall** ([ADR-015](docs/adr.md), amends ADR-013). Rest-level still detects and feeds the low tank. A pulse that has ~30 s of real air (OPEN contact, else 8 s grace) and is not shrinking — or live list past **`Tilt Critical` (default 3°, HydroHoist’s 3 in side-to-side)** — emergency-descends if the boat is above Ready: both valves open, blower off, ride to Ready, **FAULT + seal** if list has collapsed, **continue to Lowered** (vents open, `Lift Problem` ON) if residual list remains. In-move `level_fail_catchup` above Ready joins that path (no longer seals aloft). Stall still seals. **Flash-persisted `Tilt Critical` stays at 5° until nudged to 3 after OTA.** Requires OTA.
- **Lift Max is gone; Lift has two shutoffs.** One Lift command: confirmed empty → stored empty-max ceiling; any boat or unknown → Cobalt/boat ceiling. Each ceiling is approached to `Lift Target %` (default 97) so a heavier day still arrives. Unknown starts at the boat ceiling and raises the target mid-move if the raise proves empty. `cal_max` stays as Empty Max (plausibility + empty shutoff), not a mode. Web/HA **Lift Max** button and `Lift Command` option removed; panel `LIFT_MAX`/`MAX` alias to Lift. Position no longer reports `Lifted Max`. Requires OTA. Live Lift cal written to the current Cobalt-high angles (starboard −5.45°, port −4.95°) and Lift Target set to 97. Supersedes the unreleased Cobalt-only Max roof guard.
- **Raise stall timeout 30→15 s** (grace stays 20 s). Field raises never pause more than ~4 s between 0.2° progress; 15 s is still ~4× that. A dead raise now faults in ~35 s (blower off, valves closed). Flash-persisted — nudge the live slider after OTA / set via HA.
- **Red Stop button is Stop-only:** it cancels raising/lowering, aborts at-rest leveling, seals the continuously venting Lowered mode, clears FAULT, seals emergency descent, and exits Bypass. It never selects Lift Max. Press Lower again to resume Lowered venting. Dock long-hold Bypass remains removed; enter Bypass via the labeled **Bypass Mode** switch (web/HA) or panel Diagnostics only.

## [0.9.0] — 2026-08-01

### Fixed

- **Slave IMU trust now covers Lift Max.** Port gets its own persisted Lift Max angle, editable number, and capture button; that capture extends the Port plausibility window just as the existing master Lift Max capture extends Starboard. This prevents a healthy, level lift at Max from degrading to `Port outside calibrated range — ganged` merely because Max lies beyond the ordinary Lift capture. Level Status now names the exact side and whether it is offline, uncalibrated, or outside its calibrated range instead of the generic `Not trusted — ganged`.
- **Bunk Height is waterline-relative.** The arm-geometry estimate is anchored to the measured true-Lowered position of −13 in, so negative readings mean below water and positive readings mean above water instead of merely reporting rise above the Lowered calibration.
- **Ready recovery is load-aware and bounded.** A new persisted classification-confidence bit separates `Confirmed boat`, `Confirmed empty`, and `Unknown`. Manual Ready requests remain authoritative and retarget from rest or mid-move; only unattended Ready recovery requires a confirmed loaded boat. Recovery runs after the normal 3 % / 180 s / 60 s qualification and only while the lift remains above the new 5 % `Ready Recovery Floor`. Below that floor, automatic inflation is inhibited so the controller cannot pick up a potentially floating or shifted boat. Continuous level monitoring and in-move throttling remain global; parked level correction remains Lift-only.

## [0.8.1] — 2026-07-26

### Added

- **`valve_cmd` log tag**: `apply_outputs` now logs every Y1/Y2/Y3 relay command edge (`Y2 OPEN (relay energized)` …), so command-to-motion time is measurable in any log next to the `valve_fb` end-stop events.

### Documented

- **Valve opening dead time is the actuator, not the controller** (design §2 + §7.1): the relay energizes within one 250 ms tick of the button press; the 2-wire auto-return actuator charges its internal return reserve for ~5 s before driving, then travels ~2 s. The open/close asymmetry (≈7 s vs ≈2.3 s over the same 90° stroke) is the tell. Dock-observed 2026-07-26; no software fix exists, and the FSM already budgets for it.

### Fixed (device state, not code)

- Stall sliders on the dock nudged to the 0.6.0 defaults via the REST API: Stall Grace 60→20 s, Stall Timeout 120→30 s (flash-persisted values survive OTA; Blower Max Runtime was already at 4 min). The post-OTA nudge noted in 0.6.0 is now complete.

## [0.8.0] — 2026-07-25

Home Assistant control pass ([ADR-014](docs/adr.md)).

### Added

- **`Lift In Operation`** (binary, `running`, Status): ON only while a **person-initiated** move is running — any dock/panel button, the web UI, or HA — and OFF the moment the FSM settles (target reached, Stop, timeout, or fault). Machine-initiated motion (auto-maintain top-ups, ADR-013 emergency descent) deliberately does **not** light it; that motion still reads in `Lift Activity`. Backed by a new `user_cmd_move` flag set in every `request_goto_*` intent and cleared when the keeper fires a top-up.
- **`Lift Command`** (select: `— / Lift / Ready / Lower / Lift Max`): the declarative HA control — automations and scenes call `select.select_option` instead of pressing stateless buttons. Each option fires the same `request_*` intent as the matching button (all interlocks apply). Shows the destination while a user-commanded move runs, rests at `—` (no-op) so re-selecting the same destination always fires. Filed at the bottom of Advanced on the web page — the buttons remain the way to drive the lift there.

### Decided

- **No unbounded HA cover** ([ADR-014](docs/adr.md), later amended): a full-travel cover was rejected because bulk actions and position-% could lower the boat unattended. A Lift↔Ready-only cover shipped later; Lower stays on the select/button.

## [0.7.0] — 2026-07-24

### Added

- **Emergency descent on level divergence** ([ADR-013](docs/adr.md)): if the sides diverge past `Tilt Critical` (~5°) while the boat is above the Ready band, the controller no longer seals in place (which would hold the good side aloft while a failed side falls) — it opens both valves ganged, blower off, rides down to Ready, and seals there into a latched FAULT. Stop seals immediately; mode buttons are refused; trust loss doesn't stop the descent (Lower Timeout backstops and seals). New FSM state `EMERG_DESCEND` (7); new status readings `EMERGENCY — descending to Ready (level failure)` / Activity token `Emergency Descent` / panel token `EMERG_DESCEND` ("EMERGENCY"); `Lift Problem` ON throughout. At/below Ready the behaviour is unchanged (FAULT + make-safe). Trigger is deliberately only the divergence hard stop — catch-up failures and stalls still seal in place.

## [0.6.0] — 2026-07-24

Web-UI presentation pass ("why are we showing it, and is this the best way?"), the new Air Loss Alert, data-tuned safety defaults, and doc sync.

### Added

- **Air Loss Alert** (design §15.5): a `problem` binary in Status — "any sink is a symptom." Triggers (all tunable in Advanced Tuning): filtered height dropping >2 % in 30 s while both valves are commanded closed (seal failure / manually opened valve / compression spiral; latches ~30 min), parked sag rate beyond 20 %/h, or ≥4 keeper interventions in one visit. Alert-only — keepers keep correcting (ADR-004); `Air Loss Detail` (Diagnostics) + log tag `airloss` carry the reason.
- **[`docs/boat_lift_ui_reference.md`](docs/boat_lift_ui_reference.md)** — operator-facing reference: every web-UI entity explained with example readings, and the calibration workflow in plain terms.

### Changed

- **Stall detector tightened from field-data replay** (worst 0.2° progress gap in the recorded loaded *and* empty raises was 4 s): grace 60→20 s, timeout 120→30 s. A dead raise now faults in ~50 s instead of 3 min.
- **Blower Max Runtime default 5→4 min** (loaded full raise measured 134 s). Note: stall/blower values are flash-persisted on the live device — nudge the sliders once after OTA.

- **Lift Status is now the page headline** — moved to the top of the Control group, buttons directly beneath it.
- **Control buttons reordered to match the physical dock panel** (Lift / Ready / Stop / Lower), Lift Max last.
- **`Maintain Height` / `Maintain Level` renamed `Auto-Maintain Height` / `Auto-Maintain Level`** (the automation is explicit; Level covers both the in-move throttle and at-rest correction pulses). HA entity ids change; both restore to default ON.
- **Level Status rewritten simple + state-aware** (and promoted to Status): says what leveling is doing (`Leveling — holding Starboard back`, `Leveling — feeding Port`) or how level the lift is (`Level OK — Port 1.4% low`). The old `both open | err -1.4% | maintain ON` internals line is gone — valve truth lives solely in Valve Positions.
- **Maintain Observe rewritten simple**: `At Lift — 2 top-ups, 1 level fixes — sag −0.30%/h`; switch states and armed/disarmed internals dropped from the line.
- **Visit counters moved Control → Diagnostics** (they're data, not controls; kept as entities for HA graphing).
- **Lift Activity / Lift Position moved to the bottom of Status** — they exist for HA exact-match automations, not for reading.
- **Manual-valve detector now judges on end-stop evidence** with fixed constants (wrong end-stop after 5 s, or mid-travel past 15 s): the `Valve Travel Time (s)` slider is retired — nothing else consumed it.

### Removed

- **Pitch tilt proxy retired** ([ADR-012](docs/adr.md)): `Lift Tilt`, `Lift Tilt Critical`, `Calibrate: capture LEVEL ref (pitch)`, and the persisted pitch reference. Two-IMU Level Error + the level-divergence hard stop cover list end to end. `Tilt Critical (deg)` stays (hard-stop threshold).

### Fixed (docs)

- Design doc / ADR-009 now match the as-built LOWERED_VENT behaviour: the vent stays open at the bottom always; Stop there is a no-op (rev F.4).
- Documented defaults synced to the shipped config: sag deadband 4 %, `Empty Raise Rate Min` ships at the 99 ceiling (1.20 %/s is this install's tuned value).

## [0.5.0] — 2026-07-24

### Changed

- Renamed the live controller config from `boat-lift-cobalt.yaml` to `boat-lift.yaml`, and the ESPHome device to `boat-lift` / friendly name `Boat Lift` (project `ultralift.boat_lift`).
- Rewrote [`docs/boat_lift_design.md`](docs/boat_lift_design.md) as the current dual-tank as-built design (no site/boat branding, no revision changelog).
- Scrubbed personal and site-specific names from docs and YAML comments; panel weather coordinates and HA entity IDs are placeholders.
- README reframed as a generic dual-tank UltraLift reference build.

### Added

- [`docs/adr.md`](docs/adr.md) — architecture decision record for the dual-tank system (master/slave roles, maintain policy, roof guard, fail-safe blower, etc.).
- This changelog.

### Notes

- Flashing under the new `device_name` creates a **new** Home Assistant device; entity history and flash-stored calibrations under the previous name do not carry over automatically.
- MIT copyright remains Michael Cumming.

## [0.1.0] — 2026-07

### Added

- Initial public release: ESPHome dual-tank HydroHoist UltraLift automation, optional touch panel, RS485 link protocol, and field-data profiles.
