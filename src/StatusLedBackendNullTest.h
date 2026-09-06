/**
 * @file StatusLedBackendNullTest.h
 * @brief Internal host-only controls for deterministic output regression tests.
 */

#pragma once

#include "StatusLed/StatusLed.h"

#if STATUSLED_BACKEND_NULL && defined(STATUSLED_TEST)
namespace StatusLed {
namespace NullBackendTest {

struct State {
  bool ready = true;
  Status showStatus{};
  uint32_t canShowCalls = 0;
  uint32_t showCalls = 0;
  uint8_t frameCount = 0;
  RgbColor frame[StatusLed::kMaxLedCount]{};
};

// Test controls are shared between null instances; each test resets them.
State& state();
void reset();

}  // namespace NullBackendTest
}  // namespace StatusLed
#endif
