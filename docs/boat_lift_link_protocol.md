# Boat Lift ↔ Touch Panel — Wired Link Protocol

**Version:** v1
**Status:** **wired and implemented in firmware** (lift STA emitter + CMD parser live in `boat-lift.yaml`, panel end working). As-built deviation from the draft: the link runs at **9600 8N1**, not 19200.
**Applies to:** `boat-lift.yaml` (lift, KinCony KC868-A16) ⇄ `boat-lift-panel.yaml` (Waveshare ESP32-S3-Touch-LCD-4B).
**Companion:** [`boat_lift_design.md`](boat_lift_design.md) (lift control design — states, status ladder, modes; §18 covers this link).

---

## 1. Purpose & roles

The touch panel is a **request + display surface only**. It asks the lift to do
things and shows whatever the lift reports. It **never** drives an output and is
**never** a control authority.

The lift controller (A16) is the **sole authority**. It decides:

- *Can I?* (calibrated, from a valid state)
- *Is it safe?* (angle trusted, interlocks, supervisor)
- *What's actually happening?* (state, faults, position)

Everything in the lift's safety contract (design doc §11) stays on the A16. The link
carries **intents** from the panel and **status** from the lift — nothing else.

This split is the reason the panel must be **slaved to lift-reported status**,
not optimistic: the lift can *refuse* a request (uncalibrated, angle untrusted,
not from HOLD), so the panel shows "pending" and only reflects a change once the
lift's status says so.

---

## 2. Design principles

1. **Human-readable.** ASCII lines you can read on a serial monitor. No binary.
2. **Tagged, not positional.** `key=value` fields, never order-dependent CSV — so
   either side can add fields later without breaking the other.
3. **Ignore-unknown.** A receiver silently skips message types and keys it does
   not recognise. This is what makes the protocol forward-compatible.
4. **Versioned.** Every message carries `v=`. v1 today; bump only for a
   deliberate breaking change.
5. **The lift owns the truth.** The status stream is the single source of state;
   the panel never re-derives the status ladder locally.
6. **Fail safe, fail quiet.** Link loss never changes the lift's safe behaviour
   (design doc §11). A dropped frame is tolerable — the dock STOP button is the
   always-available backstop, and the panel is slaved to status so the user sees
   whether a request took.

---

## 3. Transport

### 3.1 Physical layer — RS485

The link rides **RS485** (half-duplex, differential). Both boxes already have the
hardware: the panel is the **Waveshare 4B = RS485+CAN variant**, and the
KC868-A16 has an onboard RS485 port. RS485 tolerates a dock / boathouse
environment (long-ish runs, blower inrush noise, separate supplies) far better
than bare 3.3 V TTL.

| | Value |
|---|---|
| Bus | RS485, half-duplex, 2-wire (A/B) + common ground |
| Baud | **9600 8N1** as built (draft specified 19200; slow on purpose — the traffic is tiny and noise-margin matters) |
| Framing | newline-terminated ASCII lines |

> **Why not HTTP/REST?** Control must survive a WiFi/HA outage (design doc §11:
> "network loss → no change in safe behaviour"). HTTP makes the panel a network
> appliance that dies with the network. The wired link makes it a peripheral of
> the lift. HTTP/HA stays in its lane: **weather + radar only** (see the panel's
> data split).

> **Why not CAN (yet)?** CAN is the better choice if a boathouse-wide multi-node
> bus appears. For two adjacent boxes it's overkill. The application protocol
> below is transport-agnostic, so moving it onto CAN later is a wiring change, not
> a redesign.

### 3.2 Pins (as built)

- **A16:** `GPIO13 (TX) / GPIO16 (RX)` → onboard RS485 transceiver
  (auto-direction, no DE pin).
- **Panel:** the 4B's RS485 transceiver pins — wired and working (see the panel
  config for the pin assignment).

---

## 4. Message framing

```
<TYPE> <key>=<value> <key>=<value> ... \n
```

- One message per line, terminated by `\n`. Receiver resyncs on `\n`.
- Fields are space-separated `key=value`. Order is irrelevant.
- The first token is the **message type** (`STA`, `CMD`). Unknown types: ignore.
- `msg=` (free text) is **always last** so it may contain spaces.
- No checksum in v1 — a tagged line over short RS485 is robust, and the system
  tolerates a dropped line. (A `crc=` field can be added later under rule 3/§2.)
- Malformed line → drop it, wait for the next `\n`.

---

## 5. Messages

### 5.1 `STA` — Lift → Panel (status)

Sent as a **heartbeat every ~500 ms**, even when nothing changed.

```
STA v=1 st=RAISING h=63 prob=0 trust=1 water=18.4 msg=Raising -> Lift... 63%
```

| Key | Type | Meaning |
|---|---|---|
| `v` | int | protocol version (1) |
| `st` | token | machine state token (§6) — drives colours / which control is active |
| `h` | int | lift height %, 0–100 (`-1` = unknown / not calibrated) |
| `prob` | 0/1 | Lift Problem flag (design doc §9.2) — drives the panel's error banner |
| `trust` | 0/1 | angle trusted (design doc §9.4). `0` ⇒ panel shows degraded/manual UI |
| `water` | float | water temp in **°C** (panel converts for display). `nan` if absent |
| `msg` | text | the lift's full human status string (design doc §9.2 ladder) — displayed verbatim |

Forward-compat: the panel reads the keys it knows and ignores the rest. New lift
data (e.g. `wd=` blower runtime, `cal=` calibration flags) can be appended freely.

### 5.2 `CMD` — Panel → Lift (intent)

Sent **once on a button press** (no streaming, no repeat).

```
CMD v=1 req=LIFT
```

| `req` | Lift action (existing design-doc script) | Notes |
|---|---|---|
| `LOWER` | `request_goto_lowered` | go-to Lower setpoint |
| `READY` | `request_goto_ready` | go-to Ready setpoint |
| `LIFT` | `request_goto_top` | go-to Lift setpoint |
| `LIFT_MAX` | `request_goto_max` | go-to Lift Max setpoint |
| `STOP` | `request_stop` | always honoured; no confirm |
| `RESET` | `request_reset` | clear a latched FAULT (panel gates behind a confirm) |
| `BYPASS_ON` | `enter_bypass` | deliberate hands-off override (design doc §6.6) |
| `BYPASS_OFF` | `exit_bypass` | leave bypass → HOLD |

The lift validates **every** `CMD` exactly as if it were a dock-button press —
same interlocks, same refusals. A `CMD` is never a direct output command.

Backward-compat aliases are accepted during migration: `TOP` -> `LIFT`,
`MAX` -> `LIFT_MAX`, and `LOWERED` -> `LOWER`.

Unknown `req=` value → the lift ignores it (rule 3).

---

## 6. State tokens (`st=`) → panel rendering

The token is for the panel's *visual* logic (colour, which control is live). The
full human text always comes in `msg=`.

| `st=` token | Lift condition (design doc §6.1 / §9.2) | Panel shows |
|---|---|---|
| `HOLDING` | resting, but no precise position bucket available | "HOLDING" |
| `RAISING` | MOVING_VALVE_OPENING or MOVING_UP | "RAISING" |
| `LOWERING` | MOVING_DOWN | "LOWERING" |
| `LOWERED` | resting at Lowered (floating) | "LOWERED" |
| `BETWEEN_LOWERED_READY` | between Lowered and Ready | "LOWERED/READY" |
| `READY` | resting at Ready | "READY" |
| `BETWEEN_READY_LIFTED` | between Ready and Lifted | "READY/LIFTED" |
| `LIFTED` | resting at Lifted | "LIFTED" |
| `LIFTED_MAX` | resting at Lift Max | "LIFTED MAX" |
| `BYPASS` | bypass override active | full-screen BYPASS lock |
| `FAULT` | latched fault | "ERROR" + reset affordance |

`prob=1` independently drives the error banner; `trust=0` independently drives the
degraded/manual treatment (below). They can coexist with any `st`.

---

## 7. Behaviour rules

### 7.1 Liveness / link loss
- **Lift → Panel heartbeat:** `STA` every ~500 ms.
- **Panel watchdog:** no `STA` for **~3 s** ⇒ panel shows **LINK LOST** and stops
  trusting its displayed status. (Mirrors the lift's own angle-freshness pattern.)
- **Lift side:** panel loss is **non-critical** — the lift ignores it and runs on
  its dock buttons. STOP remains physically on the dock.

### 7.2 No command ACK
The `STA` stream *is* the acknowledgment. Panel shows "pending…" on press and
confirms when the next `STA` reflects the change. If the lift refuses, `STA`
simply never flips — the honest outcome, with no extra round-trip.

### 7.3 Degraded / manual mode (`trust=0`)
A touchscreen can't be a trustworthy dead-man, so the panel does **not** offer
manual jogging. On `trust=0` it **greys out** the request buttons and shows
"MANUAL CONTROL — use dock buttons." Recovery happens at the physical dock
buttons (design doc §6.5). STOP stays available.

### 7.4 Fault (`st=FAULT` / `prob=1`)
Panel surfaces the error and offers **RESET** behind a confirm dialog → `CMD
req=RESET`. (Dock buttons can't reset; the panel and HA can.)

### 7.5 Bypass (`st=BYPASS`)
Entered deliberately from the panel's **Diagnostics** tab → `CMD req=BYPASS_ON`.
Shows a **full-screen lock** ("BYPASS ACTIVE — valve open, blower off; lift will
NOT hold the boat") that can only be left via **DISABLE BYPASS** → `CMD
req=BYPASS_OFF`. While bypassed (or moving, or faulted) the panel **never sleeps
or locks** the screen, so STOP/Disable is always one tap away.

### 7.6 Units
Temperatures cross the wire in **°C** (SI); the panel converts to °F at display.

---

## 8. Example exchange

```
# lift idling at Lifted, sensor healthy
STA v=1 st=LIFTED h=99 prob=0 trust=1 water=17.8 msg=Lifted

# user taps LOWER on the panel
CMD v=1 req=LOWER

# lift accepts, starts venting; panel flips from "pending" to LOWERING on this frame
STA v=1 st=LOWERING h=98 prob=0 trust=1 water=17.8 msg=Lowering -> Lower... 98%
STA v=1 st=LOWERING h=61 prob=0 trust=1 water=17.8 msg=Lowering -> Lower... 61%
STA v=1 st=LOWERED  h=1  prob=0 trust=1 water=17.8 msg=Lowered

# angle sensor drops out mid-rest -> panel greys go-to, points at the dock
STA v=1 st=LOWERED h=1 prob=1 trust=0 water=17.8 msg=Angle sensor OFFLINE - manual control
```

---

## 9. Open items / future fields

- Confirm RS485 transceiver pins on both boards; wire and set the `uart:`/RS485
  drive-enable on each side.
- Lift firmware: add the `STA` emitter (~2 Hz) and the `CMD` parser that maps
  `req=*` onto the existing `request_*` scripts (additive; no FSM change).
- Panel firmware: replace the local simulation with the `STA` consumer + link-loss
  watchdog; route the existing `send_cmd` seam to `uart.write`.
- Candidate future `STA` fields (append-only, ignore-unknown): `wd=` blower
  runtime, `cal=` calibration-complete flags, `lim=` top-limit state, `rx=` MPU
  RX bytes/s for remote diagnostics.
- Possible `crc=` field if RS485 noise proves it necessary.
```
