/**
 * @file StatusLedBackendNull.cpp
 * @brief Null backend for StatusLed (host tests).
 */

#include "StatusLedBackend.h"

#if STATUSLED_BACKEND_NULL

#include "StatusLedBackendNullTest.h"

#include <new>

namespace StatusLed {

#if defined(STATUSLED_TEST)
namespace NullBackendTest {
namespace {
State g_state;
}

State& state() { return g_state; }
void reset() { g_state = State(); }
}  // namespace NullBackendTest
#endif

namespace {

class BackendNull final : public BackendBase {
 public:
  Status begin(const Config&) override { return Ok(); }
  void end() override {}
#if defined(STATUSLED_TEST)
  bool canShow() const override {
    ++NullBackendTest::state().canShowCalls;
    return NullBackendTest::state().ready;
  }
  Status show(const RgbColor* frame, uint8_t count, ColorOrder) override {
    NullBackendTest::State& state = NullBackendTest::state();
    ++state.showCalls;
    if (state.showStatus.ok()) {
      state.frameCount = count;
      for (uint8_t i = 0; i < count; ++i) {
        state.frame[i] = frame[i];
      }
    }
    return state.showStatus;
  }
#else
  bool canShow() const override { return true; }
  Status show(const RgbColor*, uint8_t, ColorOrder) override { return Ok(); }
#endif
};

}  // namespace

BackendBase* createBackend() {
  return new (std::nothrow) BackendNull();
}

void destroyBackend(BackendBase* backend) {
  delete backend;
}

}  // namespace StatusLed

#endif  // STATUSLED_BACKEND_NULL
