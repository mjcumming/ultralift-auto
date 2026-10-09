#include "../finish_level.h"

#include <cassert>
#include <cmath>
#include <cstdint>

static ultralift::FinishArrival step(float abs_err, std::uint32_t now,
                                     ultralift::FinishTrimClock &clock,
                                     std::uint32_t cap_ms = 120000) {
  return ultralift::finish_arrival(abs_err, 1.5f, 0.5f, now, cap_ms, clock);
}

int main() {
  using ultralift::FinishArrival;
  using ultralift::FinishTrimClock;
  using ultralift::kFinishStallMs;
  using ultralift::kFinishTrimMs;
  using ultralift::finish_feed_throttle;
  using ultralift::raise_at_target;
  using ultralift::raise_finish_zone;

  // Last 5% of a 97% Lift target starts at 92%. At the target is not the
  // whole window.
  assert(!raise_finish_zone(91.9f, 97.0f));
  assert(raise_finish_zone(92.0f, 97.0f));
  assert(raise_finish_zone(98.1f, 97.0f));
  assert(raise_at_target(97.0f, 97.0f));
  assert(!raise_at_target(96.9f, 97.0f));
  assert(!raise_finish_zone(NAN, 97.0f));
  assert(!raise_at_target(98.0f, NAN));

  // Port low → close starboard and feed port. Port high → the other way.
  // Inside the release band, do not force a valve shut.
  assert(finish_feed_throttle(-1.46f, 0.5f) == 1);
  assert(finish_feed_throttle(1.20f, 0.5f) == 2);
  assert(finish_feed_throttle(0.31f, 0.5f) == 0);
  assert(finish_feed_throttle(-0.50f, 0.5f) == 0);
  assert(finish_feed_throttle(NAN, 0.5f) == 0);

  FinishTrimClock clock = {};

  // 2026-10-08 22:37: the raise was allowed to seal at 1.46%. The finish
  // gate has to keep feeding.
  assert(step(1.46f, 1000, clock) == FinishArrival::Hold);
  assert(clock.since == 1000);
  assert(finish_feed_throttle(-1.46f, 0.5f) == 1);
  assert(step(1.46f, 1000 + kFinishTrimMs - 1, clock) == FinishArrival::Hold);

  // Flat for the whole trim: the valve had its 20 s. Seal, do not descend.
  assert(step(1.46f, 1000 + kFinishTrimMs, clock) == FinishArrival::SealAnyway);

  // Still closing after 20 s: keep feeding.
  clock = {};
  assert(step(1.46f, 5000, clock) == FinishArrival::Hold);
  assert(step(1.20f, 5000 + 10000, clock) == FinishArrival::Hold);
  assert(clock.best < 1.30f);
  assert(step(1.00f, 5000 + kFinishTrimMs + 1000, clock) == FinishArrival::Hold);

  // Improvement stops. Five seconds with no 0.1% close, after the trim
  // window, seals. A 0.05% wiggle does not refresh the stall timer.
  const std::uint32_t stalled_at = 5000 + kFinishTrimMs + 1000;
  assert(step(0.96f, stalled_at, clock) == FinishArrival::Hold);
  assert(step(0.96f, stalled_at + kFinishStallMs - 1, clock) == FinishArrival::Hold);
  assert(step(0.96f, stalled_at + kFinishStallMs, clock) == FinishArrival::SealAnyway);

  // Inside the release band: seal and forget the trim.
  clock = {};
  step(1.20f, 8000, clock);
  assert(clock.since == 8000);
  assert(step(0.50f, 9000, clock) == FinishArrival::Seal);
  assert(clock.since == 0);
  assert(step(0.31f, 9000, clock) == FinishArrival::Seal);

  // Past the deadband: caller's catch-up, and the tight clock must not
  // already be "20 s old" if the list comes back.
  clock = {};
  step(1.20f, 1000, clock);
  assert(step(1.51f, 1000 + 250, clock) == FinishArrival::Wide);
  assert(clock.since == 0);
  assert(step(1.20f, 1000 + 500, clock) == FinishArrival::Hold);
  assert(clock.since == 1000 + 500);

  // A list that creeps 0.1% every few seconds must not run until the
  // catch-up cap. Then it seals. It does not descend.
  clock = {};
  assert(step(1.40f, 10000, clock, 120000) == FinishArrival::Hold);
  assert(step(1.20f, 10000 + 30000, clock, 120000) == FinishArrival::Hold);
  assert(step(1.00f, 10000 + 60000, clock, 120000) == FinishArrival::Hold);
  assert(step(0.80f, 10000 + 119999, clock, 120000) == FinishArrival::Hold);
  assert(step(0.70f, 10000 + 120000, clock, 120000) == FinishArrival::SealAnyway);

  // A 10 s catch-up slider still gives the tight trim its 20 s. The valve
  // is not finished opening at 10 s.
  clock = {};
  assert(step(1.20f, 2000, clock, 10000) == FinishArrival::Hold);
  assert(step(1.00f, 2000 + 10000, clock, 10000) == FinishArrival::Hold);
  assert(step(0.80f, 2000 + kFinishTrimMs - 1, clock, 10000) == FinishArrival::Hold);
  assert(step(0.70f, 2000 + kFinishTrimMs, clock, 10000) == FinishArrival::SealAnyway);

  // millis() == 0 does not stamp. The next sample does.
  clock = {0, 0, 0.0f};
  assert(step(1.20f, 0, clock) == FinishArrival::Hold);
  assert(clock.since == 0);
  assert(step(1.20f, 250, clock) == FinishArrival::Hold);
  assert(clock.since == 250);

  // A non-finite error does not start a trim.
  clock = {42, 42, 1.0f};
  assert(step(NAN, 1000, clock) == FinishArrival::Seal);
  assert(clock.since == 0);

  return 0;
}
