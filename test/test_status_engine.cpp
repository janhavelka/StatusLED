#include <unity.h>

#include "StatusLed/StatusLed.h"

static StatusLed::Config make_config() {
  StatusLed::Config cfg;
  cfg.dataPin = 1;
  cfg.ledCount = 1;
  cfg.colorOrder = StatusLed::ColorOrder::GRB;
  cfg.rmtChannel = 0;
  cfg.smoothStepMs = 20;
  return cfg;
}

static void test_blink_fast_toggles() {
  StatusLed::StatusLed leds;
  const StatusLed::Status st = leds.begin(make_config());
  TEST_ASSERT_TRUE(st.ok());

  leds.setMode(0, StatusLed::Mode::BlinkFast);

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  const StatusLed::ModeParams defaults = StatusLed::StatusLed::getModeDefaults(StatusLed::Mode::BlinkFast);
  const uint32_t onMs = defaults.onMs;
  const uint32_t periodMs = defaults.periodMs;

  leds.tick(onMs - 1);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.tick(onMs + 1);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.tick(periodMs + 1);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.end();
}

static void test_temporary_preset_reverts() {
  StatusLed::StatusLed leds;
  const StatusLed::Status st = leds.begin(make_config());
  TEST_ASSERT_TRUE(st.ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);

  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 200);
  leds.tick(10);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Error), static_cast<uint8_t>(snap.preset));
  TEST_ASSERT_TRUE(snap.tempActive);

  leds.tick(220);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready), static_cast<uint8_t>(snap.preset));
  TEST_ASSERT_FALSE(snap.tempActive);

  leds.end();
}

static void test_fade_in_oneshot() {
  StatusLed::StatusLed leds;
  const StatusLed::Status st = leds.begin(make_config());
  TEST_ASSERT_TRUE(st.ok());

  const StatusLed::ModeParams defaults = StatusLed::StatusLed::getModeDefaults(StatusLed::Mode::FadeIn);
  leds.setMode(0, StatusLed::Mode::FadeIn, defaults);

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.tick(defaults.riseMs / 2);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_GREATER_THAN_UINT8(0, snap.intensity);
  TEST_ASSERT_LESS_THAN_UINT8(255, snap.intensity);

  leds.tick(defaults.riseMs + 1);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.tick(defaults.riseMs + 500);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.end();
}

static void test_blink_fast_wraparound_does_not_freeze() {
  StatusLed::StatusLed leds;
  const StatusLed::Status st = leds.begin(make_config());
  TEST_ASSERT_TRUE(st.ok());

  const StatusLed::ModeParams defaults = StatusLed::StatusLed::getModeDefaults(StatusLed::Mode::BlinkFast);
  leds.setMode(0, StatusLed::Mode::BlinkFast, defaults);

  StatusLed::LedSnapshot snap;
  const uint32_t near_wrap = 0xFFFFFFFFu - defaults.onMs;
  leds.tick(near_wrap);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.tick(static_cast<uint32_t>(defaults.onMs + 1));
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.end();
}

static void test_fade_out_decreases_from_full_intensity() {
  StatusLed::StatusLed leds;
  const StatusLed::Status st = leds.begin(make_config());
  TEST_ASSERT_TRUE(st.ok());

  const StatusLed::ModeParams defaults = StatusLed::StatusLed::getModeDefaults(StatusLed::Mode::FadeOut);
  leds.setMode(0, StatusLed::Mode::FadeOut, defaults);

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.tick(20);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_GREATER_THAN_UINT8(200, snap.intensity);
  TEST_ASSERT_LESS_THAN_UINT8(255, snap.intensity);

  leds.tick(defaults.fallMs + 1);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.end();
}

static void test_begin_rejects_invalid_color_order_and_pin() {
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.colorOrder = static_cast<StatusLed::ColorOrder>(99);

  StatusLed::Status st = leds.begin(cfg);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG), static_cast<uint16_t>(st.code));

  cfg = make_config();
  cfg.dataPin = 300;
  st = leds.begin(cfg);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG), static_cast<uint16_t>(st.code));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_blink_fast_toggles);
  RUN_TEST(test_temporary_preset_reverts);
  RUN_TEST(test_fade_in_oneshot);
  RUN_TEST(test_blink_fast_wraparound_does_not_freeze);
  RUN_TEST(test_fade_out_decreases_from_full_intensity);
  RUN_TEST(test_begin_rejects_invalid_color_order_and_pin);
  return UNITY_END();
}
