/**
 * @file main.cpp
 * @brief Minimal ESP-IDF StatusLED example.
 */

#include <stdint.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "StatusLed/StatusLed.h"

namespace {

static constexpr char TAG[] = "statusled_idf";
static constexpr int LED_DATA_PIN = 48;
static constexpr uint8_t LED_COUNT = 1;
static constexpr uint32_t LOOP_DELAY_MS = 10U;

StatusLed::StatusLed leds;

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

}  // namespace

extern "C" void app_main(void) {
  StatusLed::Config cfg;
  cfg.dataPin = LED_DATA_PIN;
  cfg.ledCount = LED_COUNT;
  cfg.colorOrder = StatusLed::ColorOrder::GRB;
  cfg.globalBrightness = 64;
  cfg.smoothStepMs = 20;

  StatusLed::Status st = leds.begin(cfg);
  if (!st.ok()) {
    ESP_LOGE(TAG, "begin failed: %s (%d, %ld)", st.msg, static_cast<int>(st.code),
             static_cast<long>(st.detail));
    return;
  }

  st = leds.setPreset(0, StatusLed::StatusPreset::Ready);
  if (!st.ok()) {
    ESP_LOGE(TAG, "preset failed: %s (%d, %ld)", st.msg, static_cast<int>(st.code),
             static_cast<long>(st.detail));
    leds.end();
    return;
  }

  while (true) {
    leds.tick(nowMs());
    vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
  }
}
