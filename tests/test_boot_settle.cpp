#include "../boot_settle.h"

#include <cassert>
#include <cstdio>
#include <cstdint>

static ultralift::SettleGate step(bool trusted, bool under, std::uint32_t now,
                                  ultralift::SettleClocks &clocks) {
  return ultralift::boot_settle(trusted, under, now, clocks);
}

int main() {
  using ultralift::kSettleCalmMs;
  using ultralift::kSettleForceMs;
  ultralift::SettleClocks clocks = {0, 0, false};

  // No trust: nothing arms, and a stale clock is cleared.
  clocks.trust_since = 50;
  clocks.calm_since = 50;
  clocks.hard_latched = true;
  ultralift::SettleGate gate = step(false, true, 1000, clocks);
  assert(!gate.hard_stop_armed && !gate.auto_vent_allowed);
  assert(clocks.trust_since == 0 && clocks.calm_since == 0 && !clocks.hard_latched);

  // 2026-10-08: list still over the trip at 11 s. The old 8 s grace would
  // already have opened the valves. This gate must still be closed.
  clocks = {0, 0, false};
  gate = step(true, false, 0, clocks);
  assert(clocks.trust_since == 0);  // millis 0 is "not yet"; the next tick stamps
  assert(!gate.hard_stop_armed);
  gate = step(true, false, 250, clocks);
  assert(clocks.trust_since == 250);
  assert(clocks.calm_since == 0);
  gate = step(true, false, 250 + 8000, clocks);
  assert(!gate.hard_stop_armed && !gate.auto_vent_allowed);
  gate = step(true, false, 250 + 11000, clocks);
  assert(!gate.hard_stop_armed && !gate.auto_vent_allowed);

  // List falls under the limit at 14 s. Vent waits 20 s from THAT moment,
  // not from the first trusted sample.
  const std::uint32_t calm_at = 250 + 14000;
  gate = step(true, true, calm_at, clocks);
  assert(clocks.calm_since == calm_at);
  assert(!gate.auto_vent_allowed);
  gate = step(true, true, calm_at + kSettleCalmMs - 1, clocks);
  assert(!gate.hard_stop_armed && !gate.auto_vent_allowed);
  gate = step(true, true, calm_at + kSettleCalmMs, clocks);
  assert(gate.hard_stop_armed && gate.auto_vent_allowed);

  // Once armed, a list spike closes automatic vent but the hard stop stays
  // live. Re-imposing the wait would drop a hose-off that starts just after
  // settle. The calm window restarts for vent.
  gate = step(true, false, calm_at + kSettleCalmMs + 250, clocks);
  assert(gate.hard_stop_armed && !gate.auto_vent_allowed);
  assert(clocks.hard_latched);
  assert(clocks.calm_since == 0);

  // A spike back over the limit restarts the calm window. Trust does not.
  clocks = {0, 0, false};
  step(true, true, 1000, clocks);
  gate = step(true, false, 1000 + 19000, clocks);
  assert(clocks.calm_since == 0);
  assert(!gate.hard_stop_armed);
  const std::uint32_t again = 1000 + 19000;
  step(true, true, again, clocks);
  gate = step(true, true, again + kSettleCalmMs - 1, clocks);
  assert(!gate.auto_vent_allowed);
  gate = step(true, true, again + kSettleCalmMs, clocks);
  assert(gate.auto_vent_allowed && gate.hard_stop_armed);

  // List that never calms: hard stop arms at 60 s, vents stay shut.
  clocks = {0, 0, false};
  step(true, false, 1000, clocks);
  gate = step(true, false, 1000 + kSettleForceMs - 1, clocks);
  assert(!gate.hard_stop_armed && !gate.auto_vent_allowed);
  gate = step(true, false, 1000 + kSettleForceMs, clocks);
  assert(gate.hard_stop_armed && !gate.auto_vent_allowed);

  // Dropout clears both streaks. The next calm window is a full 20 s.
  gate = step(false, false, 5000, clocks);
  assert(clocks.trust_since == 0 && clocks.calm_since == 0);
  assert(!gate.hard_stop_armed);
  step(true, true, 5000, clocks);
  gate = step(true, true, 5000 + kSettleCalmMs - 1, clocks);
  assert(!gate.auto_vent_allowed);
  gate = step(true, true, 5000 + kSettleCalmMs, clocks);
  assert(gate.auto_vent_allowed);

  // millis() wrap still counts elapsed time.
  clocks = {0, 0, false};
  const std::uint32_t near_wrap = 0xFFFFF000u;
  step(true, true, near_wrap, clocks);
  gate = step(true, true, near_wrap + kSettleCalmMs, clocks);
  assert(gate.hard_stop_armed && gate.auto_vent_allowed);

  // Vent latched, then both angles come back high. One tick does not seal.
  // Two seconds of both-valid "not lowered" does. One side still down, or a
  // dropout, does not seal and a dropout does not forgive the streak.
  std::uint32_t absent = 0;
  assert(!ultralift::lowered_reading_gone(true, false, true, false, 1000, absent));
  assert(absent == 1000);
  assert(!ultralift::lowered_reading_gone(false, false, false, false, 1500, absent));
  assert(absent == 1000);
  assert(!ultralift::lowered_reading_gone(true, false, true, false, 1000 + 1999, absent));
  assert(ultralift::lowered_reading_gone(true, false, true, false, 1000 + 2000, absent));
  assert(!ultralift::lowered_reading_gone(true, true, true, false, 4000, absent));
  assert(absent == 0);
  assert(!ultralift::lowered_reading_gone(true, false, true, true, 5000, absent));
  assert(absent == 0);

  // 2026-10-09 14:30: a real Lower arrived, then the arm rang out of the
  // zone for 2 s. That reading-gone is real, and it must not seal. A
  // startup latch that was never confirmed still seals.
  assert(!ultralift::seal_unconfirmed_vent(true, true));
  assert(!ultralift::seal_unconfirmed_vent(true, false));
  assert(!ultralift::seal_unconfirmed_vent(false, false));
  assert(ultralift::seal_unconfirmed_vent(false, true));

  puts("Boot settle: 11 s slew, calm restart, 60 s force, dropout and wrap passed.");
}
