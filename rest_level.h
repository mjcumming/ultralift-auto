#pragma once

#include <cstdint>

namespace ultralift {
// Parked level correction at Lift (ADR-019).
//
// 2026-10-08 22:32 CDT, both valves open at the top of the stroke: Lift Height
// 96.5% → 90.8% in 32 s once both OPEN contacts were true, 0.18 %/s.
// One valve moves one side and is a little slower. The pulse is
// kVentPulseMultiple times the both-valve time to erase the list measured
// when the valve actually opens. The actuator's travel is not part of that
// time. When the pulse ends, the valve closes. A list that did not come
// back is not an emergency descent — Tilt Critical is.
//
// 0.18 %/s and 3×: a 2.5% list (the rest-level trigger) is a 42 s open pulse.

static const float kBothValvePctPerSec = 0.18f;
static const float kVentPulseMultiple = 3.0f;
static const std::uint32_t kValveOpenWaitMs = 25000;
static const std::uint32_t kVentPulseCapMs = 90000;

inline std::uint32_t vent_pulse_open_ms(float abs_err_pct) {
  if (!(abs_err_pct > 0.0f) || !(kBothValvePctPerSec > 0.0f)) return 0;
  float sec = kVentPulseMultiple * abs_err_pct / kBothValvePctPerSec;
  if (sec < 1.0f) sec = 1.0f;
  std::uint32_t ms = static_cast<std::uint32_t>(sec * 1000.0f);
  if (ms > kVentPulseCapMs) ms = kVentPulseCapMs;
  return ms;
}

enum class RestPulseStep : std::uint8_t { KeepOpen, Close };

struct RestPulseIn {
  bool valve_open;
  bool leveled;
  std::uint32_t since_command_ms;
  std::uint32_t since_open_ms;
  float abs_err_at_open;
};

// Keep the high-side valve commanded open, or close it. Never descends.
inline RestPulseStep rest_pulse_step(const RestPulseIn &in) {
  if (!in.valve_open) {
    if (in.since_command_ms >= kValveOpenWaitMs) return RestPulseStep::Close;
    return RestPulseStep::KeepOpen;
  }
  if (in.leveled) return RestPulseStep::Close;
  if (in.since_open_ms >= vent_pulse_open_ms(in.abs_err_at_open)) return RestPulseStep::Close;
  return RestPulseStep::KeepOpen;
}
}  // namespace ultralift
