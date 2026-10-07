#include "../cover_position.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

int main() {
  bool raised = false;
  float position = 0.0f;
  auto sample = [&](float height, float expected, bool trusted = true) {
    position = ultralift::cover_position(height, trusted, position, raised);
    assert(std::fabs(position - expected) < 0.00001f);
  };

  // Strict entry boundary; then wave/sag readings across 95% stay fully open.
  sample(95.0f, 0.95f);
  assert(!raised);
  sample(95.01f, 1.0f);
  assert(raised);
  for (float height : {96.0f, 94.8f, 95.1f, 94.9f, 93.0f, 95.0f})
    sample(height, 1.0f);

  // Missing/untrusted feedback must not change position or release the latch.
  sample(20.0f, 1.0f, false);
  sample(std::numeric_limits<float>::quiet_NaN(), 1.0f);
  sample(std::numeric_limits<float>::infinity(), 1.0f);
  sample(-std::numeric_limits<float>::infinity(), 1.0f);
  assert(raised);

  // Real descent releases below 93%; returning into the band does not re-latch.
  sample(92.99f, 0.93f);
  assert(!raised);
  sample(94.9f, 0.95f);
  assert(!raised);
  sample(40.2f, 0.40f);
  sample(-10.0f, 0.0f);
  sample(200.0f, 1.0f);

  // Restart with no restored latch; unknown data cannot fabricate fully raised.
  raised = false;
  position = 0.0f;
  sample(99.0f, 0.0f, false);
  assert(!raised);
  sample(94.0f, 0.94f);
  sample(96.0f, 1.0f);
  puts("Cover presentation: drift, boundaries, descent, invalid feedback and restart passed.");
}
