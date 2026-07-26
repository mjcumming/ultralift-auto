# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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

- **No HA cover entity** ([ADR-014](docs/adr.md)): covers invite bulk actions ("close all covers", good-night scenes, voice-assistant garage-door mapping) that could lower the boat unattended, and open/close/position-% semantics don't map to the four curated setpoints.

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
