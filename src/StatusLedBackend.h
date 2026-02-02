/**
 * @file StatusLedBackend.h
 * @brief Internal backend interface for StatusLed.
 */

#pragma once

#include "StatusLed/StatusLed.h"

namespace StatusLed {

struct BackendBase {
  virtual ~BackendBase() = default;
  virtual Status begin(const Config& config) = 0;
  virtual void end() = 0;
  virtual bool canShow() const = 0;
  virtual Status show(const RgbColor* frame, uint8_t count, ColorOrder order) = 0;
};

BackendBase* createBackend();
void destroyBackend(BackendBase* backend);

}  // namespace StatusLed
