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

// Shared RMT timings, in 25 ns ticks (40 MHz). The zero high pulse fits both
// older WS2812B and newer 220..380 ns specifications. Keep each bit at 1.25 us.
// One-bit timings retain the established waveform; datasheet revisions differ
// on T1L, so these values do not promise universal WS2812x/SK6812 compliance.
static constexpr uint16_t kRmtT0H = 13;  // 325 ns
static constexpr uint16_t kRmtT0L = 37;  // 925 ns
static constexpr uint16_t kRmtT1H = 32;  // 800 ns
static constexpr uint16_t kRmtT1L = 18;  // 450 ns

/// @brief Hardware blocks for the data, one reset symbol and the driver's EOF.
/// @param count Validated nonzero LED count.
/// @param wordsPerBlock Nonzero hardware RMT words per block.
/// @return Number of contiguous blocks required before starting transmission.
constexpr uint16_t rmtMemoryBlocksForFrame(uint8_t count, uint16_t wordsPerBlock) {
  return (static_cast<uint16_t>(count) * kBitsPerPixel + 2 + wordsPerBlock - 1) /
         wordsPerBlock;
}

/**
 * @brief Minimum idle-low time that latches a frame into the LEDs.
 *
 * The original WS2812/WS2812B datasheets ask for >50 us, but WorldSemi raised
 * it to >280 us for the WS2812B-V5 / WS2812B-2020 / WS2812C generations. The
 * 300 us reset symbol also follows the final data bit's low half, so the total
 * continuous low interval exceeds 300 us. A shorter gap can make the LEDs
 * treat the next frame as a continuation instead of latching.
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
