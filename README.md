# UltraLift Auto

**Open-source automation for a HydroHoist UltraLift hydropneumatic boat lift** — single-button go-to-position control, two-tank auto-leveling, autonomous height maintenance, physics-based boat-presence detection, and a safety-supervised state machine. Runs entirely on a $40 ESP32 relay board with [ESPHome](https://esphome.io); Home Assistant gets full visibility but is never in the safety loop.

Built for a HydroHoist **UltraLift UL2 8800** on a floating boathouse in Minaki, Ontario. Installed, calibrated, and field-proven through real boat cycles, induced-fault stress tests, and overnight unattended operation.

> ⚠️ **Safety disclaimer — read this first.** This project controls machinery that raises and lowers a multi-thousand-pound boat over water. It is shared as a reference for other builders, **not** as a turnkey product. Every lift, blower, valve, and dock is different: relay polarities, sensor signs, plumbing, and travel times in these configs are specific to one installation and several of them were discovered to be *fail-deadly* until corrected (see the design doc's history). If you adapt this, verify every output channel physically at commissioning, keep a human present until you have proven each behavior on **your** hardware, and treat the breaker as your only trustworthy off switch until you've earned confidence. You are responsible for your own installation.

## What it does

- **Go-to-position FSM** — four calibrated setpoints (Lift / Ready / Lowered / Lift Max). Press a button; the controller picks raise vs lower, drives blower + vent valves, and stops on the calibrated zone. Only Stop interrupts a move.
- **Auto-leveling (two valves, two IMUs)** — each tank gets its own vent/fill valve and inclinometer, calibrated in percent-of-span space so frame racking cancels out. While moving, the side that's ahead gets its valve throttled shut until the other catches up; divergence past a hard limit faults the lift safe.
- **Auto-maintain** — wave-filtered height observer detects sustained sag at a maintained setpoint and tops the lift back up through the normal (fully guarded) raise path. Lockout after N corrections per window — a lift that needs frequent help pages a human instead of cycling the blower forever.
- **Boat-presence detection** — no sensor needed: a loaded raise climbs ~3× slower than an empty one. The classifier times the first 15 % of climb and uses the verdict to block the roof-endangering Lift Max position whenever a boat is (or might be) aboard, including demoting an in-flight Max the moment the verdict lands.
- **Safety supervisor** — angle trust ladder (freshness + plausibility, per sensor), stall detection, absolute blower runtime cap in every mode, valve position feedback with a manual-operation detector, degraded manual mode when sensing is lost, latched faults with reasons, power-loss-safe defaults.
- **Dock UX** — four illuminated buttons with state-aware LED cadences, a decluttered on-device web UI (all tunables live-editable and flash-persisted — no recompiles to retune), and short-token status sensors (`Lift Activity`, `Lift Position`) built for exact-match Home Assistant automations.
- **Optional touch panel** — an RS485-linked ESP32-S3 display that is a pure request/display surface; every command it sends goes through the same validated intents as a physical button.

## Hardware

Full bill of materials, wiring, and I/O map: **[docs/boat_lift_cobalt_design.md](docs/boat_lift_cobalt_design.md)** (§3). The short version: a KinCony **KC868-A16** ESP32 board, two WitMotion **HWT901B-TTL** inclinometers, two 1″ stainless auto-return motorized ball valves, a 30 A relay for the OEM 115 VAC blower, four 19 mm illuminated buttons, a DS18B20, and ordinary stainless pipe fittings — everything off the shelf.

## Repo map

| File | What it is |
|---|---|
| `boat-lift-cobalt-v2.yaml` | **The live config** — two-valve / two-IMU V2, everything described above |
| `boat-lift-cobalt.yaml` | Single-valve V1, kept as a mirrored archive (not hardware-safe on split plumbing — see file header) |
| `boat-lift-panel.yaml` / `boat-lift-panel-sc01.yaml` | Touch panel UI (Waveshare 4B / WT32-SC01 Plus), runs standalone in simulation until wired |
| `rs485-test-lift.yaml` / `rs485-test-panel.yaml` | Minimal ping harnesses to prove the RS485 link independent of app logic |
| `docs/boat_lift_cobalt_design.md` | **The design document** — architecture, safety contract, failure modes, field history. Start here. |
| `docs/boat_lift_link_protocol.md` | Lift ↔ panel RS485 protocol |
| `docs/boat_lift_panel_design_revA.md` | Panel UI design |
| `field-data/` | Real captured move profiles (raise/lower CSVs, loaded + empty) used to tune the classifier and alarms |

## Getting started

1. Copy `secrets.yaml.example` → `secrets.yaml` and fill in your values.
2. Read the design doc — especially §2 (lift mechanism), §11 (safety contract), and §12 (I/O map) — and adapt pins/polarities to *your* wiring.
3. Validate and build:

   ```sh
   esphome config boat-lift-cobalt-v2.yaml
   esphome compile boat-lift-cobalt-v2.yaml
   esphome upload boat-lift-cobalt-v2.yaml   # OTA; first flash over USB
   ```

   (Developed on ESPHome 2026.6.x / ESP-IDF. V2 runs with serial logging off — UART0 feeds the second IMU — so use `esphome logs` over WiFi.)
4. Commission with the lift, not the desk: capture the four setpoints from real positions, verify each output channel physically, confirm the sensor sign convention (§2.2), and only then enable Auto-Level / Auto-Maintain — both default OFF and log shadow decisions first.

## Status

Actively used and maintained on the author's dock (summer 2026). Open items live in the design doc (§13). Issues and adaptation stories welcome.

## License

MIT — see [LICENSE](LICENSE).
