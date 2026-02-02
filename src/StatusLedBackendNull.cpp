/**
 * @file StatusLedBackendNull.cpp
 * @brief Null backend for StatusLed (host tests).
 */

#include "StatusLedBackend.h"

#if STATUSLED_BACKEND_NULL

#include <new>

namespace StatusLed {
namespace {

class BackendNull final : public BackendBase {
 public:
  Status begin(const Config&) override { return Ok(); }
  void end() override {}
  bool canShow() const override { return true; }
  Status show(const RgbColor*, uint8_t, ColorOrder) override { return Ok(); }
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
