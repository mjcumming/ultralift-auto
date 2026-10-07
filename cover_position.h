#pragma once

#include <cmath>

namespace ultralift {
// Presentation only: never use this snapped position to control lift motion.
// The latch is volatile; a reboot requalifies it from trusted live height.
inline float cover_position(float height_percent, bool trusted, float previous,
                            bool &fully_raised) {
  if (!trusted || !std::isfinite(height_percent)) return previous;
  if (height_percent > 95.0f) fully_raised = true;
  else if (height_percent < 93.0f) fully_raised = false;
  if (fully_raised) return 1.0f;
  if (height_percent < 0.0f) return 0.0f;
  return std::round(height_percent) / 100.0f;
}
}  // namespace ultralift
