/**
 * @file main.cpp
 * @brief Minimal compile-only skeleton demonstrating StatusLed lifecycle.
 *
 * This example verifies that the library compiles correctly.
 * It shows the minimal required usage pattern:
 *   1. Create instance
 *   2. Configure and call begin()
 *   3. Set a preset
 *   4. Call tick() in loop
 */

#include <Arduino.h>

#include "examples/common/BoardPins.h"
#include "StatusLed/StatusLed.h"

static StatusLed::StatusLed g_leds;
static bool g_initialized = false;

void setup() {
  StatusLed::Config config;
  config.dataPin = pins::LED_DATA;
  config.ledCount = 1;
  config.colorOrder = StatusLed::ColorOrder::GRB;
  config.rmtChannel = 0;
  config.smoothStepMs = 20;

  const StatusLed::Status st = g_leds.begin(config);
  g_initialized = st.ok();

  if (g_initialized) {
    g_leds.setPreset(0, StatusLed::StatusPreset::Ready);
  }
}

void loop() {
  if (!g_initialized) {
    return;
  }
  g_leds.tick(millis());
}
