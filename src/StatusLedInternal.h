/**
 * @file StatusLedInternal.h
 * @brief Internal helpers shared by the StatusLed output backends.
 */

#pragma once

#include <stdint.h>

#include "StatusLed/StatusLed.h"

namespace StatusLed {

/// @brief Bytes one WS2812-class pixel occupies on the wire.
static constexpr uint8_t kBytesPerPixel = 3;

/// @brief Bits one WS2812-class pixel occupies on the wire.
static constexpr uint8_t kBitsPerPixel = 24;

/**
 * @brief Minimum idle-low time that latches a frame into the LEDs.
 *
 * The original WS2812/WS2812B datasheets ask for >50 us, but WorldSemi raised
 * it to >280 us for the WS2812B-V5 / WS2812B-2020 / WS2812C generations. 300 us
 * satisfies every variant, and is what Adafruit and Espressif use. A shorter
 * gap makes the LEDs treat the next frame as a continuation of the previous
 * one, so pixel data is shifted down the chain instead of latching.
 */
static constexpr uint32_t kResetUs = 300;

/**
 * @brief Serialize one pixel into the byte order the connected LEDs expect.
 *
 * The engine's frame buffer always holds logical RGB. WS2812-class parts read
 * green first, so GRB reorders on the way out.
 *
 * @param color Logical RGB color from the frame buffer.
 * @param order Wire byte order of the connected LEDs.
 * @param out Destination buffer for exactly kBytesPerPixel bytes.
 */
inline void writePixelBytes(const RgbColor& color, ColorOrder order, uint8_t* out) {
  if (order == ColorOrder::GRB) {
    out[0] = color.g;
    out[1] = color.r;
    out[2] = color.b;
  } else {
    out[0] = color.r;
    out[1] = color.g;
    out[2] = color.b;
  }
}

}  // namespace StatusLed
