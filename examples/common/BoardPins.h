/**
 * @file BoardPins.h
 * @brief Example default pin mapping for ESP32-S2 / ESP32-S3 reference hardware.
 *
 * These are convenience defaults for our reference designs only.
 * NOT part of the library API. Override for your hardware.
 */

#pragma once

#include <stdint.h>

namespace pins {

// ====================================================================
// EXAMPLE DEFAULT PIN MAPPING - ESP32-S2 / ESP32-S3 REFERENCE HARDWARE
// ====================================================================
// These pins are NOT library defaults. They are example-only values.
// Override them for your board by creating your own BoardPins.h or
// passing explicit values to Config structs in your application.
// ====================================================================

/// @brief WS2812 data pin. Example default for ESP32-S3 (GPIO48).
/// @note ESP32-S2 commonly uses GPIO18 for onboard LED. Override for your board.
static constexpr int LED_DATA = 48;

/// @brief Default LED count for examples.
static constexpr uint8_t LED_COUNT = 3;

}  // namespace pins
