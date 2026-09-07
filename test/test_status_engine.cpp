#include <unity.h>

#include "StatusLed/StatusLed.h"
#include "../src/StatusLedBackendNullTest.h"
#include "../src/StatusLedInternal.h"
#include "../examples/common/CliParse.h"

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

static void test_status_helpers_and_accessor_aliases() {
  const StatusLed::Status ok = StatusLed::Status::Ok();
  TEST_ASSERT_TRUE(ok.ok());
  TEST_ASSERT_FALSE(ok.inProgress());
  TEST_ASSERT_TRUE(StatusLed::Ok().ok());

  const StatusLed::Status busy =
      StatusLed::Status::Error(StatusLed::Err::RESOURCE_BUSY, 12, "busy");
  TEST_ASSERT_FALSE(busy.ok());
  TEST_ASSERT_TRUE(busy.inProgress());

  const StatusLed::Status invalid =
      StatusLed::Error(StatusLed::Err::INVALID_CONFIG, 7, "bad");
  TEST_ASSERT_FALSE(invalid.ok());
  TEST_ASSERT_FALSE(invalid.inProgress());
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
                           static_cast<uint16_t>(invalid.code));
  TEST_ASSERT_EQUAL_INT32(7, invalid.detail);

  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_EQUAL_UINT8(1, leds.config().ledCount);
  TEST_ASSERT_TRUE(leds.lastStatus().ok());
  leds.end();
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

  // Run through a full SOS cycle (3400 ms) and verify it keeps going
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

// ---------------------------------------------------------------------------
// Regression tests for the v1.4.0 engine fixes
// ---------------------------------------------------------------------------

// A uint8_t phase counter used to be incremented without bound and reduced
// with "% tableLength". For tables whose length does not divide 256 the
// sequence jumped at the 255->0 wrap, corrupting the pattern roughly once a
// minute. SOS (18 steps) is the worst case.
static void test_sos_pattern_survives_phase_counter_wrap() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setMode(0, StatusLed::Mode::SOS);
  leds.setColor(0, StatusLed::RgbColor(255, 0, 0));

  static const uint32_t kStepMs[18] = {100, 100, 100, 100, 100, 300, 300, 100, 300,
                                       100, 300, 300, 100, 100, 100, 100, 100, 700};

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  uint8_t current = snap.intensity;
  uint32_t segment_start = 0;
  uint32_t segment_index = 0;

  for (uint32_t t = 1; t <= 60000; ++t) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity != current) {
      TEST_ASSERT_EQUAL_UINT32(kStepMs[segment_index % 18], t - segment_start);
      ++segment_index;
      segment_start = t;
      current = snap.intensity;
    }
  }

  // 60 s is ~17 SOS cycles, well past the 256th step where the wrap occurred.
  TEST_ASSERT_GREATER_THAN_UINT32(300, segment_index);
}

static void test_triple_blink_survives_phase_counter_wrap() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setMode(0, StatusLed::Mode::TripleBlink);
  leds.setColor(0, StatusLed::RgbColor(255, 255, 255));

  static const uint32_t kStepMs[6] = {90, 90, 90, 90, 90, 600};

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  uint8_t current = snap.intensity;
  uint32_t segment_start = 0;
  uint32_t segment_index = 0;

  for (uint32_t t = 1; t <= 60000; ++t) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity != current) {
      TEST_ASSERT_EQUAL_UINT32(kStepMs[segment_index % 6], t - segment_start);
      ++segment_index;
      segment_start = t;
      current = snap.intensity;
    }
  }

  TEST_ASSERT_GREATER_THAN_UINT32(300, segment_index);
}

// clearTemporary() used to return early when a second temporary preset was
// queued, leaving the active one running forever.
static void test_clear_temporary_cancels_active_while_another_is_pending() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 5000);
  leds.tick(10);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_TRUE(snap.tempActive);

  // Queue a second overlay on top of the active one, then cancel everything.
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Critical, 5000);
  TEST_ASSERT_TRUE(leds.clearTemporary(0).ok());
  leds.tick(20);

  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);
  TEST_ASSERT_FALSE(snap.tempPending);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready),
                          static_cast<uint8_t>(snap.preset));

  leds.end();
}

// A finished one-shot fade must not restart when a temporary preset expires.
static void test_temporary_preset_does_not_replay_finished_fade() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setColor(0, StatusLed::RgbColor(255, 255, 255));
  leds.setMode(0, StatusLed::Mode::FadeOut);
  const StatusLed::ModeParams defaults =
      StatusLed::StatusLed::getModeDefaults(StatusLed::Mode::FadeOut);

  for (uint32_t t = 0; t <= defaults.fallMs + 100; t += 20) {
    leds.tick(t);
  }

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  const uint32_t base = defaults.fallMs + 120;
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 200);
  leds.tick(base);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_TRUE(snap.tempActive);

  leds.tick(base + 250);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::FadeOut),
                          static_cast<uint8_t>(snap.mode));
  TEST_ASSERT_EQUAL_UINT8(255, snap.color.r);
  // The fade had already completed, so it must stay dark instead of restarting.
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.tick(base + 500);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.end();
}

// onMs == periodMs (or 0) used to emit a zero-length phase every period,
// which blipped the LED and retransmitted the frame twice per period.
static void test_blink_with_degenerate_duty_is_static() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  StatusLed::ModeParams always_on;
  always_on.periodMs = 100;
  always_on.onMs = 100;
  leds.setMode(0, StatusLed::Mode::BlinkSlow, always_on);

  StatusLed::LedSnapshot snap;
  for (uint32_t t = 0; t <= 1000; t += 5) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);
  }

  StatusLed::ModeParams always_off;
  always_off.periodMs = 100;
  always_off.onMs = 0;
  leds.setMode(0, StatusLed::Mode::BlinkSlow, always_off);

  for (uint32_t t = 1000; t <= 2000; t += 5) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);
  }

  leds.end();
}

// Strobe and Beacon are duty-cycle modes now, so ModeParams must apply.
static void test_strobe_and_beacon_honour_mode_params() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  StatusLed::ModeParams params;
  params.periodMs = 400;
  params.onMs = 200;
  leds.setMode(0, StatusLed::Mode::Strobe, params);

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.tick(199);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  leds.tick(200);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.tick(400);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);

  const StatusLed::ModeParams beacon =
      StatusLed::StatusLed::getModeDefaults(StatusLed::Mode::Beacon);
  TEST_ASSERT_EQUAL_UINT16(4000, beacon.periodMs);
  TEST_ASSERT_EQUAL_UINT16(80, beacon.onMs);

  leds.end();
}

// minLevel/maxLevel are documented as the output bounds; easing used to be
// applied after the interpolation, so Breathing ignored minLevel entirely.
static void test_pulse_modes_respect_level_bounds() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  StatusLed::ModeParams params;
  params.periodMs = 3000;
  params.minLevel = 20;
  params.maxLevel = 200;
  leds.setMode(0, StatusLed::Mode::Breathing, params);

  StatusLed::LedSnapshot snap;
  uint8_t lowest = 255;
  uint8_t highest = 0;
  for (uint32_t t = 0; t <= 6000; t += 10) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity < lowest) lowest = snap.intensity;
    if (snap.intensity > highest) highest = snap.intensity;
  }
  TEST_ASSERT_EQUAL_UINT8(20, lowest);
  TEST_ASSERT_EQUAL_UINT8(200, highest);

  leds.end();
}

// Pulse phase used to be derived from absolute time, so a mode set at an
// arbitrary moment started mid-cycle.
static void test_pulse_starts_at_minimum_level() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.tick(7777);
  leds.setMode(0, StatusLed::Mode::PulseSharp);
  leds.tick(7777);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);

  leds.end();
}

// Repeating deadlines advance from the previous deadline, so a slow caller
// does not stretch the cadence.
static void test_blink_cadence_does_not_drift_with_slow_ticks() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setMode(0, StatusLed::Mode::BlinkFast);  // 250 ms period, 125 ms on

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  uint8_t current = snap.intensity;
  uint32_t toggles = 0;
  for (uint32_t t = 40; t <= 10000; t += 40) {
    leds.tick(t);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    if (snap.intensity != current) {
      ++toggles;
      current = snap.intensity;
    }
  }
  // 10 s / 125 ms = 80 half-periods; latency must not eat more than a couple.
  TEST_ASSERT_GREATER_THAN_UINT32(77, toggles);

  leds.end();
}

// Explicit state changes take precedence over a running temporary preset.
static void test_set_mode_and_color_cancel_temporary_preset() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 5000);
  leds.tick(10);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_TRUE(snap.tempActive);

  leds.setMode(0, StatusLed::Mode::Solid);
  leds.setColor(0, StatusLed::RgbColor(1, 2, 3));
  leds.tick(20);

  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);

  // The temporary preset must not come back and overwrite the new state.
  leds.tick(6000);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::Mode::Solid),
                          static_cast<uint8_t>(snap.mode));
  TEST_ASSERT_EQUAL_UINT8(1, snap.color.r);
  TEST_ASSERT_EQUAL_UINT8(2, snap.color.g);
  TEST_ASSERT_EQUAL_UINT8(3, snap.color.b);

  leds.end();
}

// Per-LED brightness is independent of the temporary-preset overlay.
static void test_brightness_survives_temporary_preset_revert() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 200);
  leds.tick(10);
  leds.setBrightness(0, 77);
  leds.tick(300);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT8(77, snap.brightness);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready),
                          static_cast<uint8_t>(snap.preset));

  leds.end();
}

// A default preset must never override a running mode or a temporary preset.
static void test_default_preset_does_not_override_active_states() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.setDefaultPreset(0, StatusLed::StatusPreset::Info).ok());

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready),
                          static_cast<uint8_t>(snap.preset));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Info),
                          static_cast<uint8_t>(snap.defaultPreset));

  // While a temporary preset runs, the LED is not idle either.
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Critical, 5000);
  leds.tick(10);
  TEST_ASSERT_TRUE(leds.setDefaultPreset(0, StatusLed::StatusPreset::Warning).ok());
  leds.tick(20);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_TRUE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Critical),
                          static_cast<uint8_t>(snap.preset));

  leds.end();
}

// clear() is documented as a full state reset.
static void test_clear_resets_brightness_and_intensity() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.setBrightness(0, 33);
  leds.setDefaultPreset(0, StatusLed::StatusPreset::Info);
  leds.tick(0);

  TEST_ASSERT_TRUE(leds.clear().ok());

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.brightness);
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Off),
                          static_cast<uint8_t>(snap.defaultPreset));

  leds.end();
}

// A queued temporary preset is visible before it activates.
static void test_snapshot_reports_pending_temporary_preset() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
  leds.tick(0);
  leds.setTemporaryPreset(0, StatusLed::StatusPreset::Success, 100);

  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_TRUE(snap.tempPending);
  TEST_ASSERT_FALSE(snap.tempActive);

  leds.tick(10);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempPending);
  TEST_ASSERT_TRUE(snap.tempActive);

  leds.end();
}

// FadeIn/FadeOut are documented to use minLevel/maxLevel as their endpoints.
static void test_fade_uses_level_bounds() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());

  StatusLed::ModeParams params;
  params.riseMs = 500;
  params.minLevel = 40;
  params.maxLevel = 180;
  leds.setMode(0, StatusLed::Mode::FadeIn, params);

  StatusLed::LedSnapshot snap;
  leds.tick(0);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(40, snap.intensity);

  leds.tick(600);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(180, snap.intensity);

  leds.end();
}

static void test_output_errors_survive_success_and_reset_on_reinitialize() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_EQUAL_UINT32(0, leds.outputErrorCount());
  TEST_ASSERT_TRUE(leds.lastOutputStatus().ok());
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  auto& backend = StatusLed::NullBackendTest::state();
  backend.showStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, -37, "transmit failed");
  leds.tick(0);
  TEST_ASSERT_EQUAL_UINT32(1, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-37, leds.lastOutputStatus().detail);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::HARDWARE_FAULT),
                         static_cast<uint16_t>(leds.lastStatus().code));

  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Ready).ok());
  TEST_ASSERT_TRUE(leds.lastStatus().ok());
  TEST_ASSERT_EQUAL_UINT32(1, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-37, leds.lastOutputStatus().detail);
  TEST_ASSERT_FALSE(leds.setMode(255, StatusLed::Mode::Off).ok());
  TEST_ASSERT_EQUAL_INT32(-37, leds.lastOutputStatus().detail);

  backend.showStatus = StatusLed::Ok();
  leds.tick(100);
  TEST_ASSERT_EQUAL_UINT32(2, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(1, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-37, leds.lastOutputStatus().detail);

  StatusLed::Config invalid = make_config();
  invalid.ledCount = 0;
  TEST_ASSERT_FALSE(leds.begin(invalid).ok());
  TEST_ASSERT_TRUE(leds.isInitialized());
  TEST_ASSERT_EQUAL_UINT32(1, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_UINT32(1, backend.beginCalls);
  TEST_ASSERT_EQUAL_INT32(-37, leds.lastOutputStatus().detail);
  leds.end();
  TEST_ASSERT_EQUAL_UINT32(1, leds.outputErrorCount());
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_EQUAL_UINT32(0, leds.outputErrorCount());
  TEST_ASSERT_TRUE(leds.lastOutputStatus().ok());
  leds.tick(101);
  TEST_ASSERT_EQUAL_UINT32(3, backend.showCalls);

  // Reach the boundary in bounded time, then exercise real failed submissions.
  StatusLed::NullBackendTest::EngineAccess::seedOutputErrorCount(leds, UINT32_MAX - 1u);
  backend.showStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, -38, "fault at limit");
  leds.forceRefresh();
  leds.tick(102);
  TEST_ASSERT_EQUAL_UINT32(4, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-38, leds.lastOutputStatus().detail);
  backend.showStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, -39, "fault after limit");
  leds.tick(202);
  TEST_ASSERT_EQUAL_UINT32(5, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-39, leds.lastOutputStatus().detail);
  backend.showStatus = StatusLed::Error(StatusLed::Err::RESOURCE_BUSY, 1, "busy");
  leds.tick(302);
  TEST_ASSERT_EQUAL_UINT32(6, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-39, leds.lastOutputStatus().detail);
  backend.showStatus = StatusLed::Ok();
  leds.tick(303);
  TEST_ASSERT_EQUAL_UINT32(7, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(-39, leds.lastOutputStatus().detail);
}

static void test_busy_output_retries_without_error_and_coalesces() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  auto& backend = StatusLed::NullBackendTest::state();
  backend.showStatus = StatusLed::Error(StatusLed::Err::RESOURCE_BUSY, 0, "busy");
  leds.tick(0);
  leds.tick(1);
  TEST_ASSERT_EQUAL_UINT32(2, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(0, leds.outputErrorCount());
  TEST_ASSERT_TRUE(leds.lastOutputStatus().ok());
  TEST_ASSERT_TRUE(leds.lastStatus().ok());

  backend.ready = false;
  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Ready).ok());
  leds.tick(2);
  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Info).ok());
  leds.tick(3);
  TEST_ASSERT_EQUAL_UINT32(2, backend.showCalls);
  backend.ready = true;
  backend.showStatus = StatusLed::Ok();
  leds.tick(4);
  TEST_ASSERT_EQUAL_UINT32(3, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[0].g);
  TEST_ASSERT_EQUAL_UINT8(255, backend.frame[0].b);
  leds.tick(5);
  TEST_ASSERT_EQUAL_UINT32(3, backend.showCalls);
}

static void test_output_retry_backoff_wraps_and_keeps_animations_running() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Critical).ok());
  auto& backend = StatusLed::NullBackendTest::state();
  backend.showStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, 1, "fault");
  const uint32_t start = UINT32_MAX - 49u;
  leds.tick(start);
  TEST_ASSERT_EQUAL_UINT32(1, backend.showCalls);
  for (uint32_t elapsed = 1; elapsed < 100; ++elapsed) {
    leds.forceRefresh();
    leds.tick(start + elapsed);
  }
  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);  // Strobe toggled during backoff.
  TEST_ASSERT_EQUAL_UINT32(1, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(1, backend.canShowCalls);
  leds.tick(start + 100u);
  TEST_ASSERT_EQUAL_UINT32(2, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(2, leds.outputErrorCount());

  backend.showStatus = StatusLed::Ok();
  leds.tick(start + 199u);
  TEST_ASSERT_EQUAL_UINT32(2, backend.canShowCalls);
  leds.tick(start + 200u);
  TEST_ASSERT_EQUAL_UINT32(3, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT8(255, backend.frame[0].r);
  leds.forceRefresh();
  leds.tick(start + 201u);
  TEST_ASSERT_EQUAL_UINT32(4, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(2, leds.outputErrorCount());
}

static void test_reinitialize_clears_pending_output_retry() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  auto& backend = StatusLed::NullBackendTest::state();
  backend.showStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, 9, "fault");
  leds.tick(1000);
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  backend.showStatus = StatusLed::Ok();
  leds.tick(1001);
  TEST_ASSERT_EQUAL_UINT32(2, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(0, leds.outputErrorCount());

  backend.showStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, 10, "second fault");
  leds.forceRefresh();
  leds.tick(1002);
  TEST_ASSERT_EQUAL_UINT32(1, leds.outputErrorCount());
  TEST_ASSERT_EQUAL_INT32(10, leds.lastOutputStatus().detail);
  backend.beginStatus = StatusLed::Error(StatusLed::Err::HARDWARE_FAULT, -41, "begin failed");
  const StatusLed::Status failed = leds.begin(make_config());
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::HARDWARE_FAULT),
                         static_cast<uint16_t>(failed.code));
  TEST_ASSERT_EQUAL_INT32(-41, failed.detail);
  TEST_ASSERT_EQUAL_STRING("begin failed", failed.msg);
  TEST_ASSERT_EQUAL_UINT32(3, backend.beginCalls);
  TEST_ASSERT_FALSE(leds.isInitialized());
  TEST_ASSERT_EQUAL_UINT32(0, leds.outputErrorCount());
  TEST_ASSERT_TRUE(leds.lastOutputStatus().ok());
  TEST_ASSERT_EQUAL_INT32(-41, leds.lastStatus().detail);
  leds.tick(1003);
  TEST_ASSERT_EQUAL_UINT32(3, backend.showCalls);

  backend.beginStatus = StatusLed::Ok();
  backend.showStatus = StatusLed::Ok();
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  leds.tick(1003);
  TEST_ASSERT_EQUAL_UINT32(4, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT32(0, leds.outputErrorCount());
}

static void test_static_output_and_quantized_fades_do_not_retransmit() {
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.smoothStepMs = 5;
  TEST_ASSERT_TRUE(leds.begin(cfg).ok());
  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Ready).ok());
  auto& backend = StatusLed::NullBackendTest::state();
  leds.tick(0);
  leds.tick(1000);
  TEST_ASSERT_EQUAL_UINT32(1, backend.showCalls);
  leds.forceRefresh();
  leds.tick(1001);
  TEST_ASSERT_EQUAL_UINT32(2, backend.showCalls);
  TEST_ASSERT_TRUE(leds.setBrightness(0, 128).ok());
  leds.tick(1002);
  TEST_ASSERT_EQUAL_UINT32(3, backend.showCalls);
  TEST_ASSERT_EQUAL_UINT8(128, backend.frame[0].g);

  StatusLed::ModeParams params;
  params.riseMs = 65535;
  TEST_ASSERT_TRUE(leds.setMode(0, StatusLed::Mode::FadeIn, params).ok());
  leds.tick(1002);
  TEST_ASSERT_EQUAL_UINT32(4, backend.showCalls);
  leds.tick(1007);
  leds.tick(1012);
  TEST_ASSERT_EQUAL_UINT32(4, backend.showCalls);
}

static void test_full_configured_capacity_and_last_index() {
#if defined(STATUSLED_TEST_MAX_CAPACITY)
  static_assert(StatusLed::StatusLed::kMaxLedCount == 255,
                "native_max must compile with STATUSLED_MAX_LED_COUNT=255");
#else
  static_assert(StatusLed::StatusLed::kMaxLedCount == 10,
                "native must compile with the default capacity of 10");
#endif
  StatusLed::StatusLed leds;
  StatusLed::Config cfg = make_config();
  cfg.ledCount = StatusLed::StatusLed::kMaxLedCount;
  TEST_ASSERT_TRUE(leds.begin(cfg).ok());
  TEST_ASSERT_TRUE(leds.setAllPreset(StatusLed::StatusPreset::Ready).ok());
  const uint8_t last = static_cast<uint8_t>(cfg.ledCount - 1u);
  TEST_ASSERT_TRUE(leds.setColor(last, StatusLed::RgbColor(12, 34, 56)).ok());
  leds.tick(UINT32_MAX);
  auto& backend = StatusLed::NullBackendTest::state();
  TEST_ASSERT_EQUAL_UINT8(cfg.ledCount, backend.frameCount);
  TEST_ASSERT_EQUAL_UINT8(12, backend.frame[last].r);
  TEST_ASSERT_EQUAL_UINT8(34, backend.frame[last].g);
  TEST_ASSERT_EQUAL_UINT8(56, backend.frame[last].b);
  TEST_ASSERT_FALSE(leds.setBrightness(cfg.ledCount, 255).ok());
  TEST_ASSERT_TRUE(leds.clear().ok());
  leds.tick(0);
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[last].r);
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[last].g);
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[last].b);
}

static void test_invalid_preset_preserves_active_and_pending_overlay() {
  // Both setters must reject before touching an active or queued overlay.
  const bool temporarySetters[] = {false, true};
  for (bool temporarySetter : temporarySetters) {
    StatusLed::StatusLed leds;
    TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
    TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Ready).ok());
    leds.tick(0);
    TEST_ASSERT_TRUE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 1000).ok());
    leds.tick(10);
    const auto invalid = static_cast<StatusLed::StatusPreset>(255);
    const StatusLed::Status activeRejected = temporarySetter
        ? leds.setTemporaryPreset(0, invalid, 700)
        : leds.setPreset(0, invalid);
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
                           static_cast<uint16_t>(activeRejected.code));
    StatusLed::LedSnapshot snap;
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_TRUE(snap.tempActive);
    TEST_ASSERT_FALSE(snap.tempPending);
    TEST_ASSERT_EQUAL_UINT32(1000, snap.tempRemainingMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Error),
                           static_cast<uint8_t>(snap.preset));
    leds.tick(15);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT32(995, snap.tempRemainingMs);

    TEST_ASSERT_TRUE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Critical, 100).ok());
    const StatusLed::Status pendingRejected = temporarySetter
        ? leds.setTemporaryPreset(0, invalid, 700)
        : leds.setPreset(0, invalid);
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(StatusLed::Err::INVALID_CONFIG),
                           static_cast<uint16_t>(pendingRejected.code));
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_TRUE(snap.tempActive);
    TEST_ASSERT_TRUE(snap.tempPending);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Error),
                           static_cast<uint8_t>(snap.preset));
    leds.tick(20);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_TRUE(snap.tempActive);
    TEST_ASSERT_FALSE(snap.tempPending);
    TEST_ASSERT_EQUAL_UINT32(100, snap.tempRemainingMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Critical),
                           static_cast<uint8_t>(snap.preset));
    leds.tick(119);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_TRUE(snap.tempActive);
    TEST_ASSERT_EQUAL_UINT32(1, snap.tempRemainingMs);
    leds.tick(120);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_FALSE(snap.tempActive);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusLed::StatusPreset::Ready),
                           static_cast<uint8_t>(snap.preset));
  }
}

static void test_temporary_overlay_pauses_blink_step() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::Error).ok());
  leds.tick(0);
  leds.tick(125);
  TEST_ASSERT_TRUE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Ready, 200).ok());
  leds.tick(150);
  leds.tick(350);
  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);
  TEST_ASSERT_EQUAL_UINT8(0, StatusLed::NullBackendTest::state().frame[0].r);
  TEST_ASSERT_EQUAL_UINT8(0, StatusLed::NullBackendTest::state().frame[0].g);
  leds.tick(449);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);
  leds.tick(450);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT8(255, snap.intensity);
}

static void test_temporary_overlay_preserves_alternate_color_and_phase() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.setPreset(0, StatusLed::StatusPreset::AlarmPolice).ok());
  leds.tick(0);
  leds.tick(120);
  leds.tick(180);
  TEST_ASSERT_TRUE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Ready, 100).ok());
  leds.tick(200);
  leds.tick(300);
  auto& backend = StatusLed::NullBackendTest::state();
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[0].r);
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[0].g);
  TEST_ASSERT_EQUAL_UINT8(255, backend.frame[0].b);
  leds.tick(399);
  TEST_ASSERT_EQUAL_UINT8(255, backend.frame[0].b);
  leds.tick(400);
  TEST_ASSERT_EQUAL_UINT8(0, backend.frame[0].b);
  leds.tick(800);
  TEST_ASSERT_EQUAL_UINT8(255, backend.frame[0].r);
}

static void test_finished_fade_stays_finished_after_full_clock_wrap_and_overlay() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.setMode(0, StatusLed::Mode::FadeOut).ok());
  TEST_ASSERT_TRUE(leds.setColor(0, StatusLed::RgbColor(255, 255, 255)).ok());
  leds.tick(0);
  leds.tick(1000);
  leds.tick(0x7FFFFFFFu);
  leds.tick(0xFFFFFF00u);
  leds.tick(10);  // Same mode has now existed longer than one millis() wrap.
  TEST_ASSERT_TRUE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Ready, 100).ok());
  leds.tick(20);
  leds.tick(120);
  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT8(0, snap.intensity);
  TEST_ASSERT_EQUAL_UINT8(0, StatusLed::NullBackendTest::state().frame[0].g);
}

static void test_temporary_duration_bounds_and_wraparound() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  TEST_ASSERT_TRUE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Ready, 0x7FFFFFFFu).ok());
  TEST_ASSERT_FALSE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 0).ok());
  TEST_ASSERT_FALSE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, 0x80000000u).ok());
  TEST_ASSERT_FALSE(leds.setTemporaryPreset(0, StatusLed::StatusPreset::Error, UINT32_MAX).ok());
  const uint32_t start = UINT32_MAX - 99u;
  leds.tick(start);
  StatusLed::LedSnapshot snap;
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFFu, snap.tempRemainingMs);
  leds.tick(start + 0x7FFFFFFEu);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_EQUAL_UINT32(1, snap.tempRemainingMs);
  leds.tick(start + 0x7FFFFFFFu);
  TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
  TEST_ASSERT_FALSE(snap.tempActive);
  TEST_ASSERT_EQUAL_UINT32(0, snap.tempRemainingMs);
}

static void test_minimum_and_odd_pulse_periods_reach_level_bounds() {
  const uint16_t periods[] = {2, 3, 65535};
  for (uint16_t period : periods) {
    StatusLed::StatusLed leds;
    StatusLed::Config cfg = make_config();
    cfg.smoothStepMs = 5;
    TEST_ASSERT_TRUE(leds.begin(cfg).ok());
    StatusLed::ModeParams params;
    params.periodMs = period;
    params.minLevel = 40;
    params.maxLevel = 180;
    TEST_ASSERT_TRUE(leds.setMode(0, StatusLed::Mode::PulseSharp, params).ok());
    const uint32_t start = UINT32_MAX - 50u;
    leds.tick(start);
    StatusLed::LedSnapshot snap;
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(40, snap.intensity);
    const uint32_t peak = static_cast<uint32_t>(period) * 5u + period / 2u;
    leds.tick(start + peak);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(180, snap.intensity);
    leds.tick(start + static_cast<uint32_t>(period) * 10u);
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(40, snap.intensity);
  }
}

static void test_cli_decimal_parsing_rejects_signs_and_overflow() {
  uint32_t value = 77;
  TEST_ASSERT_TRUE(cli::parseU32("4294967295", &value));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
  const char* invalid[] = {"4294967296", "999999999999999999999999999", "-1",
                           "-4294967295", "+1", " 1", "1 ", "1x", "0x10", "", nullptr};
  for (const char* text : invalid) {
    value = 77;
    TEST_ASSERT_FALSE(cli::parseU32(text, &value));
    TEST_ASSERT_EQUAL_UINT32(77, value);
  }
  TEST_ASSERT_FALSE(cli::parseU32("1", nullptr));
  TEST_ASSERT_TRUE(cli::parseU32("0", &value));
  TEST_ASSERT_EQUAL_UINT32(0, value);
  TEST_ASSERT_TRUE(cli::parseU32("0012", &value));
  TEST_ASSERT_EQUAL_UINT32(12, value);
}

static void test_pulse_phase_survives_more_than_one_full_clock_wrap() {
  StatusLed::StatusLed leds;
  TEST_ASSERT_TRUE(leds.begin(make_config()).ok());
  StatusLed::ModeParams params;
  params.periodMs = 3000;
  TEST_ASSERT_TRUE(leds.setMode(0, StatusLed::Mode::PulseSharp, params).ok());
  const uint32_t start = 100;
  // All individual gaps stay below the deadline half range; total time spans
  // two complete millis() wraps without resetting the animation.
  for (uint64_t elapsed = 0; elapsed <= 10000000000ull; elapsed += 1000000000ull) {
    leds.tick(start + static_cast<uint32_t>(elapsed));
    const uint16_t phase = static_cast<uint16_t>(elapsed % 3000u);
    const uint8_t expected = phase < 1500u
                                 ? static_cast<uint8_t>((255u * phase) / 1500u)
                                 : static_cast<uint8_t>(255u - (255u * (phase - 1500u)) / 1500u);
    StatusLed::LedSnapshot snap;
    TEST_ASSERT_TRUE(leds.getLedSnapshot(0, &snap).ok());
    TEST_ASSERT_EQUAL_UINT8(expected, snap.intensity);
  }
}

static void test_cli_narrow_integer_parsing_preserves_output_on_failure() {
  uint8_t byte = 11;
  TEST_ASSERT_FALSE(cli::parseU8("256", &byte, 255));
  TEST_ASSERT_FALSE(cli::parseU8("-4294967295", &byte, 255));
  TEST_ASSERT_FALSE(cli::parseU8("4", &byte, 3));
  TEST_ASSERT_EQUAL_UINT8(11, byte);
  TEST_ASSERT_TRUE(cli::parseU8("255", &byte, 255));
  TEST_ASSERT_EQUAL_UINT8(255, byte);
  TEST_ASSERT_FALSE(cli::parseU8("1", nullptr, 255));
  uint16_t word = 33;
  TEST_ASSERT_FALSE(cli::parseU16("65536", &word, 0, 65535));
  TEST_ASSERT_FALSE(cli::parseU16("4", &word, 5, 1000));
  TEST_ASSERT_FALSE(cli::parseU16("1001", &word, 5, 1000));
  TEST_ASSERT_FALSE(cli::parseU16("-1", &word, 0, 65535));
  TEST_ASSERT_EQUAL_UINT16(33, word);
  TEST_ASSERT_TRUE(cli::parseU16("65535", &word, 0, 65535));
  TEST_ASSERT_EQUAL_UINT16(65535, word);
  TEST_ASSERT_TRUE(cli::parseU16("5", &word, 5, 1000));
  TEST_ASSERT_EQUAL_UINT16(5, word);
  TEST_ASSERT_FALSE(cli::parseU16("5", nullptr, 5, 1000));
}

static void test_rmt_full_frame_capacity_includes_reset_and_stop() {
  // ESP32-S3 has eight 48-symbol blocks; ESP32-S2 has four 64-symbol blocks.
  TEST_ASSERT_EQUAL_UINT16(2, StatusLed::rmtMemoryBlocksForFrame(2, 48));
  TEST_ASSERT_EQUAL_UINT16(1, StatusLed::rmtMemoryBlocksForFrame(2, 64));
  TEST_ASSERT_EQUAL_UINT16(6, StatusLed::rmtMemoryBlocksForFrame(10, 48));
  TEST_ASSERT_EQUAL_UINT16(4, StatusLed::rmtMemoryBlocksForFrame(10, 64));
  TEST_ASSERT_EQUAL_UINT16(8, StatusLed::rmtMemoryBlocksForFrame(15, 48));
  TEST_ASSERT_EQUAL_UINT16(9, StatusLed::rmtMemoryBlocksForFrame(16, 48));
  TEST_ASSERT_EQUAL_UINT16(5, StatusLed::rmtMemoryBlocksForFrame(11, 64));
  // Eight pixels occupy exactly 192 symbols: reset and stop need another block.
  TEST_ASSERT_EQUAL_UINT16(5, StatusLed::rmtMemoryBlocksForFrame(8, 48));
  TEST_ASSERT_EQUAL_UINT16(4, StatusLed::rmtMemoryBlocksForFrame(8, 64));
}

static void test_wire_color_order_and_rmt_timing_contract() {
  const StatusLed::RgbColor color(17, 34, 51);
  uint8_t bytes[] = {0, 0, 0, 0xA5};
  StatusLed::writePixelBytes(color, StatusLed::ColorOrder::GRB, bytes);
  TEST_ASSERT_EQUAL_UINT8(34, bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(17, bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(51, bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(0xA5, bytes[3]);
  StatusLed::writePixelBytes(color, StatusLed::ColorOrder::RGB, bytes);
  TEST_ASSERT_EQUAL_UINT8(17, bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(34, bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(51, bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(0xA5, bytes[3]);
  StatusLed::writePixelBytes(StatusLed::RgbColor(), StatusLed::ColorOrder::GRB, bytes);
  TEST_ASSERT_EQUAL_UINT8(0, bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0, bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(0, bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(0xA5, bytes[3]);
  TEST_ASSERT_EQUAL_UINT16(1250, 25u * (StatusLed::kRmtT0H + StatusLed::kRmtT0L));
  TEST_ASSERT_EQUAL_UINT16(1250, 25u * (StatusLed::kRmtT1H + StatusLed::kRmtT1L));
  TEST_ASSERT_TRUE(25u * StatusLed::kRmtT0H >= 250u);
  TEST_ASSERT_TRUE(25u * StatusLed::kRmtT0H <= 380u);
  TEST_ASSERT_GREATER_THAN_UINT32(280, StatusLed::kResetUs);
}

void setUp() { StatusLed::NullBackendTest::reset(); }
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_blink_fast_toggles);
  RUN_TEST(test_temporary_preset_reverts);
  RUN_TEST(test_fade_in_oneshot);
  RUN_TEST(test_blink_fast_wraparound_does_not_freeze);
  RUN_TEST(test_fade_out_decreases_from_full_intensity);
  RUN_TEST(test_begin_rejects_invalid_color_order_and_pin);
  RUN_TEST(test_status_helpers_and_accessor_aliases);
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
  RUN_TEST(test_sos_pattern_survives_phase_counter_wrap);
  RUN_TEST(test_triple_blink_survives_phase_counter_wrap);
  RUN_TEST(test_clear_temporary_cancels_active_while_another_is_pending);
  RUN_TEST(test_temporary_preset_does_not_replay_finished_fade);
  RUN_TEST(test_blink_with_degenerate_duty_is_static);
  RUN_TEST(test_strobe_and_beacon_honour_mode_params);
  RUN_TEST(test_pulse_modes_respect_level_bounds);
  RUN_TEST(test_pulse_starts_at_minimum_level);
  RUN_TEST(test_blink_cadence_does_not_drift_with_slow_ticks);
  RUN_TEST(test_set_mode_and_color_cancel_temporary_preset);
  RUN_TEST(test_brightness_survives_temporary_preset_revert);
  RUN_TEST(test_default_preset_does_not_override_active_states);
  RUN_TEST(test_clear_resets_brightness_and_intensity);
  RUN_TEST(test_snapshot_reports_pending_temporary_preset);
  RUN_TEST(test_fade_uses_level_bounds);
  RUN_TEST(test_output_errors_survive_success_and_reset_on_reinitialize);
  RUN_TEST(test_busy_output_retries_without_error_and_coalesces);
  RUN_TEST(test_output_retry_backoff_wraps_and_keeps_animations_running);
  RUN_TEST(test_reinitialize_clears_pending_output_retry);
  RUN_TEST(test_static_output_and_quantized_fades_do_not_retransmit);
  RUN_TEST(test_full_configured_capacity_and_last_index);
  RUN_TEST(test_invalid_preset_preserves_active_and_pending_overlay);
  RUN_TEST(test_temporary_overlay_pauses_blink_step);
  RUN_TEST(test_temporary_overlay_preserves_alternate_color_and_phase);
  RUN_TEST(test_finished_fade_stays_finished_after_full_clock_wrap_and_overlay);
  RUN_TEST(test_temporary_duration_bounds_and_wraparound);
  RUN_TEST(test_minimum_and_odd_pulse_periods_reach_level_bounds);
  RUN_TEST(test_pulse_phase_survives_more_than_one_full_clock_wrap);
  RUN_TEST(test_cli_decimal_parsing_rejects_signs_and_overflow);
  RUN_TEST(test_cli_narrow_integer_parsing_preserves_output_on_failure);
  RUN_TEST(test_rmt_full_frame_capacity_includes_reset_and_stop);
  RUN_TEST(test_wire_color_order_and_rmt_timing_contract);
  return UNITY_END();
}
