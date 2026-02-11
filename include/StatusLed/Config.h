/**
 * @file Config.h
 * @brief Configuration structure for StatusLed.
 *
 * All hardware-specific parameters (pins, interfaces) are injected via this
 * struct. The library never hardcodes pin values.
 */

#pragma once

#include <stdint.h>

namespace StatusLed {

/// @brief LED color byte order on the wire.
enum class ColorOrder : uint8_t {
  GRB = 0,  ///< Green, Red, Blue (typical WS2812)
  RGB = 1   ///< Red, Green, Blue
};

/**
 * @brief Configuration for StatusLed initialization.
 *
 * Pass to StatusLed::begin() to configure the library. All pin values default
 * to -1 (disabled). Set only the pins your application uses.
 *
 * @note Pin values are board-specific. Define them in your application or
 *       example code, not in the library.
 */
struct Config {
  /// @brief GPIO pin for WS2812 data output.
  /// @note Defaults to -1 (unconfigured). Must be set to a valid pin before begin().
  /// @note Application-provided. Library does not define pin defaults.
  int dataPin = -1;

  /// @brief Number of LEDs on the bus.
  /// @note Valid range: 1..10. Validated in begin().
  uint8_t ledCount = 0;

  /// @brief Color order of LEDs on the bus.
  ColorOrder colorOrder = ColorOrder::GRB;

  /// @brief RMT channel (ESP32-S2/S3: 0..3).
  /// @note Validated in begin().
  uint8_t rmtChannel = 0;

  /// @brief Global brightness scale (0..255).
  uint8_t globalBrightness = 255;

  /// @brief Minimum step period for smooth animations in milliseconds.
  /// @note Valid range: 5..1000. Lower values increase CPU usage.
  uint16_t smoothStepMs = 20;
};

}  // namespace StatusLed
