# Boat Lift Touch Panel — Interface Design (Rev A)

**Project:** Dock-mounted touchscreen remote for the *Cobalt* HydroHoist lift.
**Device:** Waveshare **ESP32-S3-Touch-LCD-4B** (480×480 IPS, ST7701 + GT911), ESPHome.
**Config:** `boat-lift-panel.yaml` · **Companion to:** `boat_lift_cobalt_design_revC.md` (the lift controller).
**Status:** UI built and on-glass. Lift motion is **mocked**; weather hooks point at real entities; radar + fault banner + wired link are **planned**.

> This doc is the design pattern + decision record for the panel UI. The lift's
> control model, modes, and status model live in revC; this doc references them
> rather than restating, and focuses on *how the panel presents and guards them.*

---

## 1. Role & Boundaries

The panel is a **remote display + control surface**, not a brain. All lift
intelligence (FSM, safety supervisor, calibration) stays on the lift's ESP32
(see revC §4). The panel **emits intents and renders state** — if it dies, the
lift still works from its dock buttons.

**Design rule:** the panel must never *look* operable when it can't actually
command the lift. Loss of the link → controls disabled + visible "offline".

---

## 2. Data Architecture (two independent paths)

| Path | Carries | Dependency |
|---|---|---|
| **Direct wired link to the lift ESP32** (UART / RS-485 / CAN — TBD) | Lift Status, Lift Problem/faults, Lift Height/position, angle-trust, **Water Temp**, + outgoing mode/stop commands | **None** (no HA). Safety-critical. |
| **Home Assistant** (`homeassistant:` import) | Air temp, wind, rain chance, humidity, UV, pressure, **radar** | Opportunistic; degrades to "--" when HA is down |

**Decision — why control is *not* through HA:** a boat lift is a crush hazard;
the control path must survive a network/HA outage. HA is for *nice-to-haves*
only. This also keeps a pure ESPHome workflow. (Bus/protocol for the wired link
is not yet chosen — see §9.)

---

## 3. Confirmed Hardware

- **Board:** ESP32-S3-Touch-LCD-4B, 16 MB flash, 8 MB octal PSRAM.
- **Display:** `mipi_rgb`, model `WAVESHARE-4-480X480` (mainline ESPHome 2026.6 —
  auto-fills CS=GPIO42, RGB data pins, timing, ST7701 init, 18-bit). Needs an
  `spi:` bus (clk=GPIO2 / mosi=GPIO1) for the init sequence only.
- **Touch:** GT911 @ I²C `0x5D`.
- **I²C bus:** SDA=GPIO15, SCL=GPIO7. Also present: PCF85063 RTC @ `0x51`.
- **IO expander:** **CH422G @ `0x3C`** (mainline `ch422g`, no address field). EXIO
  map: **1=TP_RST, 2=LCD backlight/DISP, 3=LCD_RST**.
  - Gotcha: CH422G defaults all outputs LOW → reset/backlight must be driven
    high or screen is dark / touch held in reset.
  - **Not** the CH32V003-hub variant the community 4-inch configs assume.

---

## 4. Interaction Model

### 4.1 Anti-accident strategy (capacitive-touch aware)
A wet cap-touch screen registers **phantom touches** (rain, condensation, spray).
Defense in depth:

1. **Idle lock + slide-to-unlock.** After 90 s idle → backlight off + lock page.
   Wake on touch → **slide-to-unlock** (a full-drag slider; a droplet can't
   complete the gesture). Tabs/controls are unreachable while locked.
2. **Confirm on motion.** Every go-to command pops a **confirm dialog** naming the
   target ("Send lift to TOP?" / "GO TO TOP"). A single stray tap can't move the
   lift. **STOP is the exception** — immediate, no confirm.
3. **(Planned) Button-disable** when a move isn't allowed (faulted / angle
   untrusted / link offline), with the reason shown — panel never offers an
   action the lift will refuse (revC §6.4).

**Decision — presets over hold-to-run:** commercial lifts use deadman hold-to-run,
but cap-touch can't reliably "hold" when wet. So we use **go-to-position presets
+ confirm + prominent STOP**, and rely on the lift-side supervisor, limit
switches, and a hardwired e-stop for hard safety (revC §6).

### 4.2 Screen wake / backlight
Backlight = CH422G EXIO2. Idle (90 s) drives it off; **any touch wakes it**
(GT911 still reads while dark). No external proximity sensor (board has none).

---

## 5. Control Surface (mirrors lift design revC §5)

Three **go-to-position** modes + Stop. The panel sends the target; the lift picks
raise vs lower automatically.

The panel buttons use **action-verb labels** (what you're doing), laid out
left→right as **down→up**; the lift's *positions/status* keep the design-doc
nouns (Top/Ready/Lowered). Colors are tied to the mode, not the word.

| Button (verb) | Position | Color | Target (mock %) | Real source |
|---|---|---|---|---|
| **LOWER** (left) | Lowered | 🟢 Green `0x27AE60` | 0 | `cal_lowered` |
| **READY** (mid) | Ready | 🟠 Orange `0xE0792E` | 20 | `cal_ready` (near bottom) |
| **RAISE** (right) | Top | 🔵 Blue `0x2E86DE` | 100 | `cal_top` |
| **STOP** (bar) | — | 🔴 Red `0xC0392B` | — (halt) | — |

**Decision — verbs not nouns on the buttons:** "LIFT" would collide with the
bottom **LIFT tab**; action verbs (RAISE/LOWER) also read more clearly as
*commands*. Status text still uses the position nouns to match the lift's
`Lift Status` (`AT TOP / AT READY / AT LOWERED`).

**Conventions:**
- **Red is reserved for STOP only.** No other control uses red.
- Button **colors match the dock-button LEDs** (revC §10); labels are verbs while
  the lift's status stays in position-nouns for cross-interface consistency.
- Status line uses revC language: `AT TOP / AT READY / AT LOWERED / BETWEEN`
  when resting, `RAISING / LOWERING` while moving.
- **(Planned)** mirror the dock LED scheme (revC §8): active mode highlighted,
  target pulses while moving, STOP glows red in motion, red-pulse = critical.

---

## 6. Screen Layout (three tabs + overlays)

Navigation = a **persistent bottom tab bar** (LIFT / WEATHER / RADAR), hidden
while locked. Tabs are finger-sized (≥46 px tall). Two overlays float on the
`top_layer`: the **confirm dialog** and the **tab bar**.

```
 LOCK            LIFT                  WEATHER             RADAR
 +----------+   +-----------------+   +---------------+   +-------------+
 |          |   | time/date | MODE|   | WEATHER—Minaki|   |             |
 | BOAT     |   |-----------------|   | condition     |   |   full-     |
 |  LIFT    |   | AIR    WATER  ▐ |   | wind / rain   |   |   screen    |
 |          |   | LIFT OK       ▐ |   | humidity / UV |   |   radar     |
 | (slide   |   |    [ STOP ]     |   | pressure      |   |  (camera.   |
 |  to      |   | [TOP][RDY][LOW] |   | rise / set    |   |kenora_radar)|
 | unlock)  |   +-----------------+   +---------------+   +-------------+
 +----------+   [ LIFT ][WEATHER][RADAR]  <- tab bar on all three
```

- **LIFT** — control-focused: title bar (time/date left, current **mode** right,
  with dividers), air + water temp, lift-health line *(placeholder for §7
  banner)*, vertical height bar (right), STOP, the three modes.
- **WEATHER** — condition, wind+gust, rain chance, humidity, UV, pressure,
  sunrise/sunset. Sun times computed locally via `sun:` (Minaki 49.9864,
  −94.6700) so they survive an HA outage.
- **RADAR** — full canvas for `camera.kenora_radar` (radar is detail-dense;
  it gets the whole screen, minus the tab strip). Periodically-refreshed still.

### 6.1 Visual language
- **Background** `0x0E1116`; panels `0x1B2230`; dividers `0x2A3340`; muted text
  `0x6B7686`.
- **Accents:** air = amber `0xF2C14E`, water = green `0x4FE08A`, lift level/% =
  cyan `0x00A6FF`, rain = light-blue `0x7FB4FF`.
- **Fonts:** built-in Montserrat (14/16/20/22/24/26/28/30); one gfonts
  `font_val` (40) for temperature values needing the `°` glyph.

---

## 7. Status & Fault Display (planned — sourced from the wired link)

Maps revC §9 (health) / §10 (entities) onto the panel:

1. **Primary status** in the LIFT title bar — driven by real **Lift Status**.
2. **Fault/health banner** — invisible when healthy; on a problem shows a
   color-coded strip with the **Lift Problem** reason:
   - 🟠 **Amber** = degraded (angle STALE / OUT-OF-RANGE → manual recovery,
     auto-moves blocked, revC §9.4).
   - 🔴 **Red** = FAULT (stall / blower watchdog / runtime cap / valve-position
     anomaly) with reason text.
3. **Button-disable** when a move is disallowed (revC §6.4 preconditions); banner
   says why.
4. **LIFT OFFLINE failsafe** — keyed off a **heartbeat on the wired link** (frames
   stop arriving) → grey controls + "LIFT OFFLINE". *Not* tied to HA.
5. **(Future)** a Diagnostics view for revC §10 items (angle trust, MPU RX
   bytes/s, top-limit, raw state, blower runtime, WiFi, uptime).

---

## 8. Home Assistant Inputs (Environment Canada — Kenora station)

| Panel field | Entity |
|---|---|
| Air temp | `sensor.kenora_temperature` |
| Wind speed / dir / gust | `sensor.kenora_wind_speed` / `…_wind_direction` / `…_wind_gust` |
| Rain chance | `sensor.kenora_chance_of_precipitation` |
| Condition / humidity / UV / pressure | `sensor.kenora_current_condition` / `…_humidity` / `…_uv_index` / `…_barometric_pressure` |
| Alerts | `sensor.kenora_warnings` / `…_watches` / `…_advisories` / `…_statements` |
| **Radar** | `camera.kenora_radar` (EC integration composites map+precip → one image) |

**Planned weather-alert banner** (same pattern as the lift banner, different
source): non-empty warnings/watches → 🔴/🟠 strip with text. Useful near water —
a thunderstorm/wind warning is a "get the boat up now" signal.

---

## 9. Open Decisions / TODO

- **Wired-link bus + protocol:** direct UART (3-wire, co-mounted) vs RS-485 vs
  CAN; and the frame format (lift → panel status/height/temp/problem; panel →
  lift mode/stop). Lift firmware doesn't emit frames yet.
- **Wire the placeholders:** radar image (`online_image` ← HA camera snapshot),
  the four EC weather detail fields, the lift fault banner + button-disable.
- **Backlight-off-on-idle caveat:** if EXIO2 is the panel DISP (not the LED
  backlight), verify wake redraws cleanly rather than showing garbage.
- **Enclosure:** out of sun/rain (owner-confident); cap-touch wet behavior to be
  observed in situ.

---

## 10. Build / Flash

Per the project recipe (`venv311` + PowerShell + hand-placed cmake +
`GIT_CEILING_DIRECTORIES=C:/Users/mike/.platformio`). Device on **COM5**.
First clean ESP-IDF build ≈ minutes; cached ≈ ~130 s.
