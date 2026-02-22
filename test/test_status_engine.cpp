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

static void test_methods_reject_when_not_initialized() {
  StatusLed::StatusLed leds;

  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.setMode(0, StatusLed::Mode::Solid).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.setColor(0, StatusLed::RgbColor(255, 0, 0)).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.setPreset(0, StatusLed::StatusPreset::Ready).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.setBrightness(0, 128).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.setGlobalBrightness(128).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.clear().code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.clearTemporary(0).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::NOT_INITIALIZED),
      static_cast<uint16_t>(leds.setAllPreset(StatusLed::StatusPreset::Ready).code));
}

static void test_index_out_of_range_rejected() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(leds.setMode(5, StatusLed::Mode::Solid).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(leds.setColor(5, StatusLed::RgbColor(255, 0, 0)).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(leds.setBrightness(5, 128).code));
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(leds.clearTemporary(5).code));

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(leds.getLedSnapshot(5, &snap).code));

  leds.end();
}

static void test_clear_resets_all_state() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Error);
  leds.tick(0);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::BlinkFast), static_cast<uint8_t>(snap.mode));

  TEST_ASSERT_TRUE(leds.clear().ok());
  leds.tick(1);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::Off), static_cast<uint8_t>(snap.mode));
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.r);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.g);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.b);

  leds.end();
}

static void test_clear_temporary_reverts_early() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);

  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 5000);
  leds.tick(10);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_TRUE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Error), static_cast<uint8_t>(snap.preset));

  TEST_ASSERT_TRUE(leds.clearTemporary(0).ok());
  leds.tick(20);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready), static_cast<uint8_t>(snap.preset));

  leds.end();
}

static void test_clear_temporary_noop_when_inactive() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);

  TEST_ASSERT_TRUE(leds.clearTemporary(0).ok());

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready), static_cast<uint8_t>(snap.preset));

  leds.end();
}

static void test_set_all_preset_applies_to_all_leds() {
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.ledCount = 3;
  TEST_ASSERT_TRUE(leds.begin(cfg).ok());

  TEST_ASSERT_TRUE(leds.setAllPreset(StatusLed::StatusPreset::Warning).ok());
  leds.tick(0);

  for (uint8_t i = 0; i < 3; ++i) {
    StatusLed::LedSnapshot snap;
    TEST_ASSERT_TRUE(leds.getLedSnapshot(i, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StatusLed::StatusPreset::Warning),
        static_cast<uint8_t>(snap.preset));
  }

  leds.end();
}

static void test_set_all_preset_rejects_unknown() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  StatusLed::Status st = leds.setAllPreset(static_cast<StatusLed::StatusPreset>(99));
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(st.code));

  leds.end();
}

static void test_flicker_candle_does_not_freeze() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setMode(0, StatusLed::Mode::FlickerCandle);
  leds.setColor(0, StatusLed::RgbColor(255, 128, 0));

  StatusLed::LedSnapshot snap;
  uint8_t last_intensity = 0;
  bool changed = false;
  for (uint32_t t = 0; t < 2000; t += 10) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity != last_intensity) {
      changed = true;
    }
    last_intensity = snap.intensity;
  }
  TEST_ASSERT_TRUE(changed);

  leds.end();
}

static void test_glitch_mode_produces_variation() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setMode(0, StatusLed::Mode::Glitch);
  leds.setColor(0, StatusLed::RgbColor(255, 255, 255));

  bool saw_on = false;
  bool saw_off = false;
  StatusLed::LedSnapshot snap;
  for (uint32_t t = 0; t < 5000; t += 10) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity == 255) saw_on = true;
    if (snap.intensity == 0) saw_off = true;
  }
  TEST_ASSERT_TRUE(saw_on);
  TEST_ASSERT_TRUE(saw_off);

  leds.end();
}

static void test_multiple_leds_independent() {
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.ledCount = 3;
  TEST_ASSERT_TRUE(leds.begin(cfg).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.setPreset(1, StatusLed::StatusPreset::Error);
  leds.setPreset(2, StatusLed::StatusPreset::Info);
  leds.tick(0);

  StatusLed::LedSnapshot s0, s1, s2;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &s0).ok());
  TEST_ASSERT_TRUE(leds.getLedSnapshot(1, &s1).ok());
  TEST_ASSERT_TRUE(leds.getLedSnapshot(2, &s2).ok());

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::Solid), static_cast<uint8_t>(s0.mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::BlinkFast), static_cast<uint8_t>(s1.mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::Solid), static_cast<uint8_t>(s2.mode));

  TEST_ASSERT_EQUAL_UINT8(0, s0.color.r);
  TEST_ASSERT_EQUAL_UINT8(255, s0.color.g);
  TEST_ASSERT_EQUAL_UINT8(255, s1.color.r);
  TEST_ASSERT_EQUAL_UINT8(0, s1.color.g);
  TEST_ASSERT_EQUAL_UINT8(0, s2.color.r);
  TEST_ASSERT_EQUAL_UINT8(0, s2.color.g);
  TEST_ASSERT_EQUAL_UINT8(255, s2.color.b);

  leds.end();
}

static void test_tick_without_begin_does_not_crash() {
  StatusLed::StatusLed leds;
  leds.tick(0);
  leds.tick(100);
  leds.tick(0xFFFFFFFF);
  TEST_ASSERT_FALSE(leds.isInitialized());
}

static void test_reinitialize_after_end() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.isInitialized());
  leds.end();
  TEST_ASSERT_FALSE(leds.isInitialized());
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.isInitialized());
  leds.end();
}

static void test_default_preset_applies_when_idle() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  TEST_ASSERT_TRUE(leds.setDefaultPreset(0, StatusLed::StatusPreset::Info).ok());
  leds.tick(0);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Info),
                           static_cast<uint8_t>(snap.preset));

  leds.end();
}

static void test_brightness_scaling() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.setBrightness(0, 128);
  leds.tick(0);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(128, snap.brightness);
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.end();
}

static void test_sos_mode_pattern() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setMode(0, StatusLed::Mode::SOS);
  leds.setColor(0, StatusLed::RgbColor(255, 0, 0));

  StatusLed::LedSnapshot snap;

  // First step: dot on (100ms)
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  // After first dot: off (100ms)
  leds.tick(101);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  // Run through a full SOS cycle (~4200ms) and verify it keeps going
  bool saw_on = false;
  bool saw_off = false;
  for (uint32_t t = 4300; t < 8500; t += 50) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity == 255) saw_on = true;
    if (snap.intensity == 0) saw_off = true;
  }
  TEST_ASSERT_TRUE(saw_on);
  TEST_ASSERT_TRUE(saw_off);

  leds.end();
}

static void test_success_preset() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Success).ok());
  leds.tick(0);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Success), static_cast<uint8_t>(snap.preset));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::DoubleBlink), static_cast<uint8_t>(snap.mode));
  // Green color
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.r);
  TEST_ASSERT_EQUAL_UINT8(255, snap.color.g);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.b);

  leds.end();
}

static void test_connecting_preset() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Connecting).ok());
  leds.tick(0);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Connecting), static_cast<uint8_t>(snap.preset));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::PulseSoft), static_cast<uint8_t>(snap.mode));
  // Blue color
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.r);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.g);
  TEST_ASSERT_EQUAL_UINT8(255, snap.color.b);

  leds.end();
}

static void test_lowbattery_preset() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::LowBattery).ok());
  leds.tick(0);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::LowBattery), static_cast<uint8_t>(snap.preset));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::Beacon), static_cast<uint8_t>(snap.mode));
  // Red color
  TEST_ASSERT_EQUAL_UINT8(255, snap.color.r);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.g);
  TEST_ASSERT_EQUAL_UINT8(0, snap.color.b);

  leds.end();
}

static void test_set_all_mode_applies_to_all() {
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.ledCount = 3;
  TEST_ASSERT_TRUE(leds.begin(cfg).ok());

  TEST_ASSERT_TRUE(leds.setAllMode(StatusLed::Mode::BlinkSlow).ok());
  leds.tick(0);

  for (uint8_t i = 0; i < 3; ++i) {
    StatusLed::LedSnapshot snap;
    TEST_ASSERT_TRUE(leds.getLedSnapshot(i, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::BlinkSlow), static_cast<uint8_t>(snap.mode));
  }

  leds.end();
}

static void test_set_all_mode_rejects_invalid() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  StatusLed::Status st = leds.setAllMode(static_cast<StatusLed::Mode>(99));
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
      static_cast<uint16_t>(st.code));

  leds.end();
}

static void test_set_all_color_applies_to_all() {
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.ledCount = 3;
  TEST_ASSERT_TRUE(leds.begin(cfg).ok());

  TEST_ASSERT_TRUE(leds.setAllMode(StatusLed::Mode::Solid).ok());
  TEST_ASSERT_TRUE(leds.setAllColor(StatusLed::RgbColor(100, 200, 50)).ok());
  leds.tick(0);

  for (uint8_t i = 0; i < 3; ++i) {
    StatusLed::LedSnapshot snap;
    TEST_ASSERT_TRUE(leds.getLedSnapshot(i, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(100, snap.color.r);
    TEST_ASSERT_EQUAL_UINT8(200, snap.color.g);
    TEST_ASSERT_EQUAL_UINT8(50, snap.color.b);
  }

  leds.end();
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
  RUN_TEST(test_methods_reject_when_not_initialized);
  RUN_TEST(test_index_out_of_range_rejected);
  RUN_TEST(test_clear_resets_all_state);
  RUN_TEST(test_clear_temporary_reverts_early);
  RUN_TEST(test_clear_temporary_noop_when_inactive);
  RUN_TEST(test_set_all_preset_applies_to_all_leds);
  RUN_TEST(test_set_all_preset_rejects_unknown);
  RUN_TEST(test_flicker_candle_does_not_freeze);
  RUN_TEST(test_glitch_mode_produces_variation);
  RUN_TEST(test_multiple_leds_independent);
  RUN_TEST(test_tick_without_begin_does_not_crash);
  RUN_TEST(test_reinitialize_after_end);
  RUN_TEST(test_default_preset_applies_when_idle);
  RUN_TEST(test_brightness_scaling);
  RUN_TEST(test_sos_mode_pattern);
  RUN_TEST(test_success_preset);
  RUN_TEST(test_connecting_preset);
  RUN_TEST(test_lowbattery_preset);
  RUN_TEST(test_set_all_mode_applies_to_all);
  RUN_TEST(test_set_all_mode_rejects_invalid);
  RUN_TEST(test_set_all_color_applies_to_all);
  return UNITY_END();
}
