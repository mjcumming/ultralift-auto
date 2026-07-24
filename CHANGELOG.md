# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
