#pragma once

#include <cmath>
#include <cstdint>

namespace ultralift {
// Raise finish. The climb keeps the normal deadband. The last slice of a
// raise does not.
//
// 2026-10-08 22:37 CDT, re-raise onto the boat ceiling: at 95% the list was
// already back to +0.3% (inside the 0.5% release). The port valve was still
// opening, starboard kept climbing, and at the 97% target the list was
// −1.46%. That is inside the 1.5% deadband, so the raise sealed. Port never
// got the last percent. The release itself was not waiting on the 15 s
// min-hold — a release is already immediate. What sealed the gap was the
// completion test using the wide deadband, and a direction change near the
// ceiling still having to wait out the hold.
//
// Inside kFinishWindowPct of the target, a throttle change is not held.
// At the target, |list| must be inside the in-move release (deadband −
// hysteresis, 0.5% at the current sliders) before the raise seals. A gap
// still inside the deadband is fed on the low side. If that trim has had
// kFinishTrimMs and the list has not improved by kFinishImprovePct for
// kFinishStallMs, seal anyway. A gap past the deadband is unchanged:
// the existing catch-up timer may still descend.

static const float kFinishWindowPct = 5.0f;
static const float kFinishImprovePct = 0.1f;
static const std::uint32_t kFinishTrimMs = 20000;
static const std::uint32_t kFinishStallMs = 5000;

inline bool finish_elapsed(std::uint32_t now, std::uint32_t since, std::uint32_t need) {
  return since != 0 && (now - since) >= need;
}

inline bool raise_at_target(float pct, float target) {
  return std::isfinite(pct) && std::isfinite(target) && pct >= target;
}

inline bool raise_finish_zone(float pct, float target) {
  return std::isfinite(target) && raise_at_target(pct, target - kFinishWindowPct);
}

// 0 = both valves follow the move, 1 = close master (feed slave),
// 2 = close slave (feed master). Same codes as level_throttle.
inline int finish_feed_throttle(float err, float release) {
  if (!(release >= 0.0f) || !std::isfinite(err)) return 0;
  if (err > release) return 2;
  if (err < -release) return 1;
  return 0;
}

enum class FinishArrival : std::uint8_t {
  Seal,        // |err| is inside the release band
  Hold,        // keep raising; feed the low side
  SealAnyway,  // trim stalled, or ran out its cap, still inside the deadband
  Wide,        // |err| is past the deadband; caller runs catch-up
};

struct FinishTrimClock {
  std::uint32_t since;    // 0 = not in a tight trim
  std::uint32_t best_ms;  // last improvement
  float best;             // lowest |err| this trim
};

inline FinishArrival finish_arrival(float abs_err, float deadband, float release,
                                    std::uint32_t now, std::uint32_t catchup_timeout_ms,
                                    FinishTrimClock &clock) {
  if (!(abs_err >= 0.0f) || !(deadband > 0.0f)) {
    clock = {};
    return FinishArrival::Seal;
  }
  if (!(release >= 0.0f)) release = 0.0f;
  if (release > deadband) release = deadband;

  if (abs_err <= release) {
    clock = {};
    return FinishArrival::Seal;
  }
  if (abs_err > deadband) {
    clock = {};
    return FinishArrival::Wide;
  }

  if (clock.since == 0) {
    // millis() == 0 is "not stamped". The next tick stamps. Same rule as
    // the boot-settle clocks.
    if (now == 0) return FinishArrival::Hold;
    clock.since = now;
    clock.best_ms = now;
    clock.best = abs_err;
    return FinishArrival::Hold;
  }
  if (clock.best - abs_err > kFinishImprovePct) {
    clock.best = abs_err;
    clock.best_ms = now;
  }

  // A short catch-up slider must not cut the trim off while the valve is
  // still traveling. The wide-gap descent keeps the slider as it is.
  std::uint32_t cap = catchup_timeout_ms;
  if (cap < kFinishTrimMs) cap = kFinishTrimMs;
  const bool aged = finish_elapsed(now, clock.since, kFinishTrimMs);
  const bool stalled = finish_elapsed(now, clock.best_ms, kFinishStallMs);
  const bool capped = finish_elapsed(now, clock.since, cap);
  if (capped || (aged && stalled)) {
    clock = {};
    return FinishArrival::SealAnyway;
  }
  return FinishArrival::Hold;
}
}  // namespace ultralift
