/**
 * @file StatusLedInternal.h
 * @brief Internal helpers for StatusLed implementation.
 */

#pragma once

#include "StatusLed/StatusLed.h"

namespace StatusLed {

inline RgbColor mapColorOrder(const RgbColor& color, ColorOrder srcOrder, ColorOrder dstOrder) {
  if (srcOrder == dstOrder) {
    return color;
  }
  return RgbColor(color.g, color.r, color.b);
}

}  // namespace StatusLed
