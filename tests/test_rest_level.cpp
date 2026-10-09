#include "../rest_level.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>

int main() {
  using ultralift::kValveOpenWaitMs;
  using ultralift::kVentPulseCapMs;
  using ultralift::RestPulseIn;
  using ultralift::RestPulseStep;
  using ultralift::rest_pulse_step;
  using ultralift::vent_pulse_open_ms;

  // 2.5% list, both-valve rate 0.18 %/s, 3× → 41.667 s.
  assert(vent_pulse_open_ms(2.5f) == 41666u);
  // Scales with the list. A 5% list (about the 3° wall on this arm) is double.
  assert(vent_pulse_open_ms(5.0f) == 83333u);
  assert(vent_pulse_open_ms(0.0f) == 0u);
  assert(vent_pulse_open_ms(-2.0f) == 0u);
  assert(vent_pulse_open_ms(NAN) == 0u);
  // A nonsense snapshot cannot hold a valve open past the cap.
  assert(vent_pulse_open_ms(100.0f) == kVentPulseCapMs);

  RestPulseIn in = {};
  in.abs_err_at_open = 2.5f;

  // Actuator still traveling. Tonight's port valve took 18 s. That is not "no air".
  in.valve_open = false;
  in.since_command_ms = 18000;
  assert(rest_pulse_step(in) == RestPulseStep::KeepOpen);
  in.since_command_ms = kValveOpenWaitMs - 1;
  assert(rest_pulse_step(in) == RestPulseStep::KeepOpen);
  in.since_command_ms = kValveOpenWaitMs;
  assert(rest_pulse_step(in) == RestPulseStep::Close);

  // Open, list flat, still inside the pulse: stay open. Do not descend.
  in.valve_open = true;
  in.leveled = false;
  in.since_command_ms = 20000;
  in.since_open_ms = vent_pulse_open_ms(2.5f) - 1;
  assert(rest_pulse_step(in) == RestPulseStep::KeepOpen);

  // Same flat list at the end of the pulse: close.
  in.since_open_ms = vent_pulse_open_ms(2.5f);
  assert(rest_pulse_step(in) == RestPulseStep::Close);

  // Level reached early: close, even if the budget has not elapsed.
  in.since_open_ms = 1000;
  in.leveled = true;
  assert(rest_pulse_step(in) == RestPulseStep::Close);

  std::puts("Rest-level pulse: open-wait, flat list, and early level passed.");
  return 0;
}
