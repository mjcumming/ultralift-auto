# Embedded web dashboard

October 6, 2026: the everyday page puts position commands, Stop and current lift status first. Calibration is already established and belongs under **Advanced → Calibration**, closed by default. Tuning and service/bench controls are also nested inside Advanced. Diagnostics has its own closed, read-only section. Nothing must be expanded for normal operation.

## Everyday view

- Lift, Ready, Guide and Lower send the existing stateless button requests. A separate red Stop stays visible in the sticky command bar, including while scrolling through Advanced. There is no arbitrary-position slider on the everyday page. The Home Assistant cover still ignores a partial position.
- Lift Status supplies the headline. Lift Position supplies the position badge and, while the lift is parked, the steady highlight. During a person-commanded move, `Lift Command` slow-flashes that destination and the button for the place you left stays dark. A keeper top-up does not flash. Pressing a button does not invent a completed move. If the live stream has gone quiet, returning to the tab reconnects and takes a fresh snapshot.
- Water temperature is prominent at the top of the status panel, with the device-provided unit. Cards show calibrated Lift Height, estimated Bunk Height, a concise Level status with its detail, and read-only Operations: blower **command** On/Off, Starboard valve and Port valve. Valve readings use both end-stop inputs: Open, Closed, Moving (neither limit), Fault (both limits), or Unknown (missing feedback). Per-side percentages and numeric level error remain in Diagnostics. The height bar represents the Lowered-to-Lift calibration span; the numeric reading can exceed 100% when empty or fall below zero.
- Boat Load State retains Confirmed/Unknown wording. Temperature uses the unit supplied by the device. Last Stop Reason and Maintain Observe explain recent activity. Auto-Maintain Height and Level remain available on the main page.
- Fault, emergency, air-loss, Bypass and bench conditions appear prominently. During fault/emergency/Bypass/bench, ordinary destination buttons are disabled; Stop remains available while connected. When the existing firmware reports degraded manual control, the page explains manual Lift/Lower motion and disables Ready/Guide. A single dead IMU (`port angle` or `port IMU offline` in the status line) keeps destination buttons available and explains which sensor is carrying height. All actual permission checks stay in firmware.

## Connection and request handling

The JavaScript and CSS are embedded via `web_server.js_include` and `css_include`, with remote asset URLs blank. The page needs neither a CDN nor Home Assistant. ESPHome web-server v3 and its existing entity groups are retained; the stock frontend is replaced by the bundled page.

The client consumes `/events`. Discovery metadata provides entity names and number bounds. Subsequent state-only events merge with that metadata. REST paths use URL-encoded names from the controller, preserving spaces, punctuation and side labels instead of guessing routes from legacy slugs. Calibration and tuning numbers, and the ten capture buttons, remain under Advanced. Target Height and Go sit at the bottom of Service & bench, not in the tuning grid and not on the everyday height card. No calibration keys or defaults change.

Disconnect/error or 15 seconds without any event marks the page offline, disables commands and hides current readings behind unavailable labels. Reconnection clears cached entity states and waits for fresh discovery. Commands are never queued, retried or replayed on reconnection. A lost response explicitly says the request may already have reached the controller.

Stop can be sent while another request is pending. It aborts the older browser request and supersedes its notification; this cannot undo an already delivered action. A successful HTTP response means a **request was sent**, not that physical motion or stopping was verified. Switch/number requests read back the reported value; unchanged readback is described as pending/unconfirmed. Editing a number is preserved across ordinary telemetry updates and requires its explicit Save button.

## Advanced controls

Calibration retains both sides' capture buttons and editable angles. Tuning retains the remaining numbers with device-provided minimum, maximum, step and unit. Opening a section has no control side effects. Captures explicitly replace the corresponding saved position.

Service contains Restart, Bypass and Bench Test. The three direct relay controls are disabled in the page until bench mode is reported active. Inject level fail is labeled as a test that can move the lift, and is disabled during bench mode. At the bottom of that section, a bunk-inch target and Go send one height command; Go stays disabled while Bench Test is on. This page restriction does not add authentication or replace backend interlocks. Motion, calibration, dock-button and panel behavior is unchanged. The HA cover display uses the hysteresis below.

## Home Assistant cover position

`Lift Ready` reports 100% once trusted, calibrated height exceeds 95%. It stays at 100% until height falls **below 93%**; exactly 93% retains the raised latch. Returning into the band does not re-latch until height again exceeds 95%. This two-percentage-point hysteresis absorbs small drift and waves around the old boundary without concealing larger descent. Raw Lift Height, arrival thresholds, safety and maintenance logic do not use the snapped position.

Untrusted or non-finite height holds the last reported position and latch. The latch is not persisted; boot qualifies it from live feedback. Normal partial positions retain whole-percent rounding and the zero floor. Existing Lift/Ready/Stop actions, operation reporting and ignored partial-position commands are unchanged.

The hardware-independent helper is exercised by `tests/test_cover_position.cpp` (compile with a C++11-or-newer compiler). Cases cover entry/release boundaries, repeated jitter, real descent, missing/non-finite input, empty-lift readings and restart.

## Local preview and verification

Use the repository's ESPHome 2026.6.x virtual environment:

```powershell
.\venv\Scripts\esphome.exe config boat-lift.yaml
.\venv\Scripts\esphome.exe compile boat-lift.yaml
.\venv\Scripts\python.exe tests/preview_dashboard.py
```

The preview binds only to `127.0.0.1:8767`. It reads entity metadata from `boat-lift.yaml`, substitutes the side names, and serves simulated state. It never reads secrets or connects to a device. Its header explicitly identifies simulated readings.

Open `/` or `/?scene=moving`, `/?scene=fault`, `/?scene=emergency`, `/?scene=unknown`, `/?scene=port`, or `/?scene=valves` (contradictory/traveling valve feedback). `/?scene=slow` delays number responses for three seconds so Stop can be tested during a pending save. The fixture deliberately sends initial full metadata, then state-only updates, and uses opaque entity IDs to catch accidental slug-based routing. `/test/requests` records browser requests; `/test/drop` interrupts the simulated event stream. Reload `/` to reconnect.

Check destination/Stop routing, switch readback, decimal number saves, all 37 numbers/ten captures, relay controls disabled outside bench, visible bench/fault/emergency notices, disconnected controls, collapsed defaults, and mobile layout. These are UI tests, not tests of the lift state machine or real equipment. The preview never sends hardware commands. Firmware upload is separate from running this fixture.

## Initial dashboard verification — October 6, 2026

ESPHome 2026.6.2 configuration validation and the final firmware compile passed. A structural comparison against the prior YAML found only the six web presentation options changed; all controller and calibration configuration matched. Browser checks covered decimal setting readback, all 37 number controls and ten capture buttons, bench-only relay controls, Stop during a pending save, fault/emergency/manual/unknown states, disconnection, reconnection without replay, and collapsed defaults. The 390-pixel phone view had no horizontal overflow, with Stop visible while scrolling through calibration. Final preview console contained no warnings or errors. Readings and requests were simulated; no lift was contacted or flashed.


## Deployed refinement — October 6, 2026

Built with ESPHome 2026.6.2 and uploaded successfully by OTA to `boat-lift` at `192.168.4.44`, after confirming Idle, blower off, both valves closed, bench off and Bypass off. Firmware compilation stamp: `2026-10-06 13:49:09 -0500`; the Firmware Build sensor reports `Oct 6 2026 13:50:34`. Flash use is 55.4%; RAM use is 18.2%.

The standalone C++ cover tests passed for drift, strict 95% entry / 93% release boundaries, descent, invalid feedback, out-of-range height and restart. A parsed YAML comparison against the original configuration found only the web presentation options, helper include, volatile cover latch and cover-position mapping changed; the rest of the control and calibration configuration matched.

Browser checks covered the revised desktop and 390-pixel phone layout, prominent temperature (including unavailable readings), concise level/adjustment/offline messages, and read-only valve Closed/Open/Moving/Fault/Unknown states. No horizontal overflow or browser warnings/errors were found. Advanced remained collapsed by default.

After upload, the native API confirmed the new build, Idle, blower off, closed valves and a fully raised cover. All 37 saved number settings and both Auto-Maintain switches matched the snapshot immediately before upload. Decompressed JavaScript and CSS served by the controller matched the included source after ESPHome's newline normalization. The live page showed water temperature 15.5 °C, height about 99%, and both valves Closed. No motion or calibration commands were sent during deployment verification; threshold jitter was exercised locally, not by moving the boat.
