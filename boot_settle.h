#pragma once

#include <cstdint>

namespace ultralift {
// Automatic valve-open after power-up.
//
// Dock 2026-10-08: a power restore opened both vents while the boat was at
// Lift. One early frame satisfied "at Lowered", and the level filter was
// still 3.8° at 11 s, so an 8 s hard-stop grace had already expired.
//
// auto_vent_allowed: both IMUs trusted AND |list| under Tilt Critical for
//   kSettleCalmMs. Lowered-vent re-entry and rest-level wait for this.
// hard_stop_armed: that calm window, OR both IMUs trusted for kSettleForceMs
//   even if the list never calms (a real twist that is still there).
//
// trust_since / calm_since are 0 when that streak is not running. Stamp them
// from the same millis() sample the caller uses as `now` — never from a
// later millis(), or unsigned subtraction looks like the window already
// elapsed. A millis() wrap at ~49 days still elapses correctly.
struct SettleClocks {
  std::uint32_t trust_since;
  std::uint32_t calm_since;
  // Stays set once the hard stop has armed. A later list spike must not
  // re-impose the wait; only a loss of dual-IMU trust clears it.
  bool hard_latched;
};

struct SettleGate {
  bool hard_stop_armed;
  bool auto_vent_allowed;
};

static const std::uint32_t kSettleCalmMs = 20000;
static const std::uint32_t kSettleForceMs = 60000;

inline bool settle_elapsed(std::uint32_t now, std::uint32_t since, std::uint32_t need) {
  return since != 0 && (now - since) >= need;
}

inline SettleGate boot_settle(bool both_trusted, bool list_under, std::uint32_t now,
                              SettleClocks &clocks) {
  if (!both_trusted) {
    clocks.trust_since = 0;
    clocks.calm_since = 0;
    clocks.hard_latched = false;
    return {false, false};
  }
  if (clocks.trust_since == 0) clocks.trust_since = now;
  if (list_under) {
    if (clocks.calm_since == 0) clocks.calm_since = now;
  } else {
    clocks.calm_since = 0;
  }
  const bool calm = settle_elapsed(now, clocks.calm_since, kSettleCalmMs);
  const bool forced = settle_elapsed(now, clocks.trust_since, kSettleForceMs);
  if (calm || forced) clocks.hard_latched = true;
  SettleGate gate;
  gate.hard_stop_armed = clocks.hard_latched;
  gate.auto_vent_allowed = calm;
  return gate;
}

// LOWERED_VENT whose "at the bottom" reading has gone away. Both IMUs must
// be valid and neither in the Lowered zone for kLoweredAbsentMs. A dropout
// does not count: unknown is not "the boat is up". One side still lowered
// does not count. Used to close the valves if a startup frame latched the
// vent and the angles then came back high.
static const std::uint32_t kLoweredAbsentMs = 2000;

inline bool lowered_reading_gone(bool stbd_valid, bool stbd_lowered,
                                 bool port_valid, bool port_lowered,
                                 std::uint32_t now, std::uint32_t &absent_since) {
  if (!stbd_valid || !port_valid) return false;
  if (stbd_lowered || port_lowered) {
    absent_since = 0;
    return false;
  }
  if (absent_since == 0) {
    absent_since = now;
    return false;
  }
  return settle_elapsed(now, absent_since, kLoweredAbsentMs);
}

// A commanded Lower that has reached the zone keeps the vent open through
// the arrival ring. 2026-10-09 14:30 CDT: the lower touched 54.2°, rebounded
// to 48.1° for 2 s, the seal closed both valves, and re-entry opened them
// again at 51.9°. The 2 s seal remains for a vent latched without that
// arrival — a power-up frame whose angles then come back high.
inline bool seal_unconfirmed_vent(bool arrived, bool reading_gone) {
  return !arrived && reading_gone;
}
}  // namespace ultralift
