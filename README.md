# UltraLift Auto

**Open-source automation for a HydroHoist UltraLift hydropneumatic boat lift** — single-button go-to-position control, dual-tank auto-leveling, autonomous height maintenance, physics-based boat-presence detection, and a safety-supervised state machine. Runs entirely on a KinCony KC868-A16 ESP32 relay board with [ESPHome](https://esphome.io); Home Assistant gets full visibility but is never in the safety loop.

Built for a HydroHoist **UltraLift UL2 8800** with **per-tank valves and inclinometers**. Field-proven through real boat cycles, induced-fault stress tests, and overnight unattended operation.

> ⚠️ **Safety disclaimer — read this first.** This project controls machinery that raises and lowers a multi-thousand-pound boat over water. It is shared as a reference for other builders, **not** as a turnkey product. Every lift, blower, valve, and dock is different: relay polarities, sensor signs, plumbing, and travel times in these configs are specific to one installation and several of them were discovered to be *fail-deadly* until corrected (see [`docs/adr.md`](docs/adr.md)). If you adapt this, verify every output channel physically at commissioning, keep a human present until you have proven each behavior on **your** hardware, and treat the breaker as your only trustworthy off switch until you've earned confidence. You are responsible for your own installation.

## What it does

- **Go-to-position FSM** — four calibrated setpoints (Lift / Ready / Lowered / Lift Max). Press a button; the controller picks raise vs lower, drives blower + vent valves, and stops on the calibrated zone. Mode buttons retarget mid-move (last press wins); Stop cancels.
- **Auto-Maintain Height / Auto-Maintain Level** (default ON) — height top-ups at Ready, Lift, and Lift Max; in-move throttle on every go-to plus at-rest leveling only while parked at Lift. Per-visit counters on `Maintain Observe`. No sticky lockout that disables keeping.
- **Two-valve / two-IMU leveling** — each tank has its own vent/fill valve and inclinometer; divergence past a hard limit faults the lift safe.
- **Boat-presence detection** — no sensor needed: a loaded raise climbs much slower than an empty one. The classifier times the first 15 % of climb and uses the verdict to block the roof-endangering Lift Max position whenever a boat is (or might be) aboard, including demoting an in-flight Max the moment the verdict lands.
- **Safety supervisor** — angle trust ladder (freshness + plausibility, per sensor), stall detection, absolute blower runtime cap in every mode, valve position feedback with a manual-operation detector, degraded manual mode when sensing is lost, latched faults with reasons, power-loss-safe defaults.
- **Dock UX** — four illuminated buttons with state-aware LED cadences, a decluttered on-device web UI (all tunables live-editable and flash-persisted — no recompiles to retune), and short-token status sensors (`Lift Activity`, `Lift Position`) built for exact-match Home Assistant automations.
- **Optional touch panel** — an RS485-linked ESP32-S3 display that is a pure request/display surface; every command it sends goes through the same validated intents as a physical button.

## Hardware

Full bill of materials, wiring, and I/O map: **[docs/boat_lift_design.md](docs/boat_lift_design.md)** (§3). The short version: a KinCony **KC868-A16** ESP32 board, two WitMotion **HWT901B-TTL** inclinometers, two 1″ stainless auto-return motorized ball valves, a 30 A relay for the OEM 115 VAC blower, four 19 mm illuminated buttons, a DS18B20, and ordinary stainless pipe fittings — everything off the shelf.

## Repo map

| File | What it is |
| --- | --- |
| `boat-lift.yaml` | **The live config** — two-valve / two-IMU controller described above |
| `boat-lift-panel.yaml` | Touch panel UI (Waveshare ESP32-S3-Touch-LCD-4B), runs standalone in simulation until wired |
| `rs485-test-lift.yaml` / `rs485-test-panel.yaml` | Minimal ping harnesses to prove the RS485 link independent of app logic |
| `docs/boat_lift_design.md` | **The design document** — architecture, safety contract, failure modes. Start here. |
| `docs/boat_lift_ui_reference.md` | **Web UI & calibration reference** — what every entity means, and how to calibrate |
| `docs/adr.md` | Design decisions and rationale |
| `docs/boat_lift_link_protocol.md` | Lift ↔ panel RS485 protocol |
| `docs/boat_lift_panel_design_revA.md` | Panel UI design |
| `field-data/` | Captured move profiles (raise/lower CSVs, loaded + empty) used to tune the classifier and alarms |

## Getting started

1. Copy `secrets.yaml.example` → `secrets.yaml` and fill in your values.
2. Read the design doc — especially §2 (lift mechanism), §11 (safety contract), and §12 (I/O map) — and adapt pins/polarities to *your* wiring. Skim [`docs/adr.md`](docs/adr.md) for the non-obvious choices.
3. Activate the project venv (ESPHome **2026.6.x** — do not use a global `esphome` on PATH; that may be an older install), then validate and build:

   ```sh
   # Windows
   .\venv\Scripts\Activate.ps1
   esphome version          # expect 2026.6.x
   esphome config boat-lift.yaml
   esphome compile boat-lift.yaml
   esphome upload boat-lift.yaml --device OTA   # or --device <lift-ip>
   ```

   (Serial logging is off — UART0 feeds the second IMU — so use `esphome logs` over WiFi.)
4. Commission with the lift, not the desk: capture the four master setpoints (and three slave captures) from real positions, verify each output channel physically, confirm the sensor sign convention (§2.2). **Auto-Maintain Height** and **Auto-Maintain Level** (Control section switches, default ON) — confirm both on a supervised first night, and watch **Visit Height Top-ups** / **Visit Level Events** (Diagnostics).

> **Note on device naming:** this repo uses `device_name: boat-lift`. If you previously flashed under a different ESPHome device name, Home Assistant will see a **new** device; entity history and flash-stored calibrations under the old name do not carry over automatically.

## Status

Reference implementation for dual-tank UltraLift automation. Open items live in the design doc (§13). Issues and adaptation stories welcome.

## License

MIT — see [LICENSE](LICENSE).
