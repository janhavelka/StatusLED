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
// Override them with STATUSLED_EXAMPLE_DATA_PIN / STATUSLED_EXAMPLE_LED_COUNT
// build flags, or pass explicit values to Config in your application.
// ====================================================================

/// @brief WS2812 data pin. Example default for the reference boards (GPIO21).
/// @note ESP32-S3-DevKitC-1 onboard LED is GPIO48, ESP32-S2-Saola-1 is GPIO18. Override for your board.
#ifdef STATUSLED_EXAMPLE_DATA_PIN
static constexpr int LED_DATA = STATUSLED_EXAMPLE_DATA_PIN;
#else
static constexpr int LED_DATA = 21;
#endif

/// @brief Default LED count for examples.
#ifdef STATUSLED_EXAMPLE_LED_COUNT
static_assert(STATUSLED_EXAMPLE_LED_COUNT >= 1 && STATUSLED_EXAMPLE_LED_COUNT <= 255,
              "STATUSLED_EXAMPLE_LED_COUNT must be in 1..255");
static constexpr uint8_t LED_COUNT = STATUSLED_EXAMPLE_LED_COUNT;
#else
static constexpr uint8_t LED_COUNT = 2;
#endif

}  // namespace pins
