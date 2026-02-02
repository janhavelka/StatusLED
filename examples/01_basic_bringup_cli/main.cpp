/**
 * @file main.cpp
 * @brief Deprecated example. Use 01_status_led_cli instead.
 */

#include <Arduino.h>

#include "examples/common/Log.h"

void setup() {
  log_begin(115200);
  Serial.println(F("This example is deprecated. Use examples/01_status_led_cli."));
}

void loop() {}
