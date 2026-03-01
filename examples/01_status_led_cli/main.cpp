/**
 * @file main.cpp
 * @brief Interactive CLI example for StatusLed.
 *
 * Demonstrates library lifecycle with serial commands.
 * Type 'help' for available commands.
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "examples/common/BoardPins.h"
#include "examples/common/Log.h"
#include "StatusLed/StatusLed.h"
#include "StatusLed/Version.h"

static StatusLed::StatusLed g_leds;
static StatusLed::Config g_config;
static bool g_initialized = false;

struct StressState {
  bool active = false;
  uint16_t periodMs = 50;
  uint32_t nextMs = 0;
  uint32_t step = 0;
};

static StressState g_stress;

static char g_line[128];
static uint8_t g_line_len = 0;

static void print_help_section(const char* title) {
  Serial.printf("%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
}

static void print_help_item(const char* cmd, const char* desc) {
  Serial.printf("  %s%-42s%s - %s\n", LOG_COLOR_CYAN, cmd, LOG_COLOR_RESET, desc);
}

struct ModeNameMap {
  const char* name;
  StatusLed::Mode mode;
};

struct PresetNameMap {
  const char* name;
  StatusLed::StatusPreset preset;
};

static const ModeNameMap kModes[] = {
  {"off", StatusLed::Mode::Off},
  {"solid", StatusLed::Mode::Solid},
  {"dim", StatusLed::Mode::Dim},
  {"blinkslow", StatusLed::Mode::BlinkSlow},
  {"blinkfast", StatusLed::Mode::BlinkFast},
  {"doubleblink", StatusLed::Mode::DoubleBlink},
  {"tripleblink", StatusLed::Mode::TripleBlink},
  {"beacon", StatusLed::Mode::Beacon},
  {"strobe", StatusLed::Mode::Strobe},
  {"fadein", StatusLed::Mode::FadeIn},
  {"fadeout", StatusLed::Mode::FadeOut},
  {"pulsesoft", StatusLed::Mode::PulseSoft},
  {"pulsesharp", StatusLed::Mode::PulseSharp},
  {"breathing", StatusLed::Mode::Breathing},
  {"heartbeat", StatusLed::Mode::Heartbeat},
  {"throb", StatusLed::Mode::Throb},
  {"flicker", StatusLed::Mode::FlickerCandle},
  {"glitch", StatusLed::Mode::Glitch},
  {"alternate", StatusLed::Mode::Alternate},
  {"sos", StatusLed::Mode::SOS},
};

static const PresetNameMap kPresets[] = {
  {"off", StatusLed::StatusPreset::Off},
  {"ready", StatusLed::StatusPreset::Ready},
  {"busy", StatusLed::StatusPreset::Busy},
  {"warning", StatusLed::StatusPreset::Warning},
  {"error", StatusLed::StatusPreset::Error},
  {"critical", StatusLed::StatusPreset::Critical},
  {"updating", StatusLed::StatusPreset::Updating},
  {"info", StatusLed::StatusPreset::Info},
  {"maintenance", StatusLed::StatusPreset::Maintenance},
  {"police", StatusLed::StatusPreset::AlarmPolice},
  {"hazard", StatusLed::StatusPreset::HazardAmber},
  {"success", StatusLed::StatusPreset::Success},
  {"connecting", StatusLed::StatusPreset::Connecting},
  {"lowbat", StatusLed::StatusPreset::LowBattery},
};

static const char* mode_name(StatusLed::Mode mode) {
  for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); ++i) {
    if (kModes[i].mode == mode) {
      return kModes[i].name;
    }
  }
  return "unknown";
}

static const char* preset_name(StatusLed::StatusPreset preset) {
  for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
    if (kPresets[i].preset == preset) {
      return kPresets[i].name;
    }
  }
  return "unknown";
}

static bool parse_mode(const char* s, StatusLed::Mode* out) {
  for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); ++i) {
    if (strcmp(kModes[i].name, s) == 0) {
      *out = kModes[i].mode;
      return true;
    }
  }
  return false;
}

static bool parse_preset(const char* s, StatusLed::StatusPreset* out) {
  for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
    if (strcmp(kPresets[i].name, s) == 0) {
      *out = kPresets[i].preset;
      return true;
    }
  }
  return false;
}

static bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !*s) {
    return false;
  }
  char* end = nullptr;
  const unsigned long val = strtoul(s, &end, 10);
  if (end == s) {
    return false;
  }
  *out = static_cast<uint32_t>(val);
  return true;
}

static int split_args(char* line, char* argv[], int max_args) {
  int argc = 0;
  char* p = line;
  while (*p && argc < max_args) {
    while (*p && isspace(static_cast<unsigned char>(*p))) {
      ++p;
    }
    if (!*p) {
      break;
    }
    argv[argc++] = p;
    while (*p && !isspace(static_cast<unsigned char>(*p))) {
      ++p;
    }
    if (*p) {
      *p = '\0';
      ++p;
    }
  }
  return argc;
}

static char* read_line() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      g_line[g_line_len] = '\0';
      g_line_len = 0;
      return g_line;
    }
    if (g_line_len + 1 < sizeof(g_line)) {
      g_line[g_line_len++] = c;
    }
  }
  return nullptr;
}

static void print_help() {
  Serial.println();
  Serial.printf("%s=== StatusLed CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  Serial.print(F("Version: "));
  Serial.println(StatusLed::VERSION);
  Serial.print(F("Built:   "));
  Serial.println(StatusLed::BUILD_TIMESTAMP);
  Serial.print(F("Commit:  "));
  Serial.print(StatusLed::GIT_COMMIT);
  Serial.print(F(" ("));
  Serial.print(StatusLed::GIT_STATUS);
  Serial.println(F(")"));
  Serial.println();
  print_help_section("Common");
  print_help_item("help", "Show this help");
  Serial.println();
  print_help_section("Lifecycle");
  print_help_item("begin [pin] [count] [grb|rgb] [rmt] [smooth_ms]", "Initialize driver");
  print_help_item("end", "Stop driver");
  print_help_item("stress on [period_ms]", "Enable mixed stress mode");
  print_help_item("stress off", "Disable stress mode");
  Serial.println();
  print_help_section("Inspect");
  print_help_item("status [index]", "Show LED snapshot (one or all)");
  print_help_item("config", "Print active configuration");
  print_help_item("last", "Print last driver status");
  print_help_item("list_modes", "List mode names");
  print_help_item("list_presets", "List preset names");
  Serial.println();
  print_help_section("Control");
  print_help_item("mode <i> <mode>", "Set mode");
  print_help_item("modep <i> <mode> <period> <on> <rise> <fall> <min> <max>", "Set mode with params");
  print_help_item("color <i> <r> <g> <b>", "Set primary color");
  print_help_item("alt <i> <r> <g> <b>", "Set secondary color");
  print_help_item("preset <i> <preset>", "Set current preset");
  print_help_item("default <i> <preset>", "Set default preset");
  print_help_item("temp <i> <preset> <duration_ms>", "Set temporary preset");
  print_help_item("bright <i> <level>", "Set LED brightness");
  print_help_item("gbright <level>", "Set global brightness");
  print_help_item("clear", "Clear all LEDs");
  print_help_item("cleartemp <i>", "Clear temporary state");
  print_help_item("allpreset <preset>", "Set preset on all LEDs");
  print_help_item("allmode <mode>", "Set mode on all LEDs");
  print_help_item("allcolor <r> <g> <b>", "Set color on all LEDs");
  print_help_item("refresh", "Force refresh");
  Serial.println();
}

static void print_config() {
  Serial.print(F("dataPin="));
  Serial.print(g_config.dataPin);
  Serial.print(F(" ledCount="));
  Serial.print(g_config.ledCount);
  Serial.print(F(" order="));
  Serial.printf("%s%s%s",
                LOG_COLOR_CYAN,
                g_config.colorOrder == StatusLed::ColorOrder::GRB ? "GRB" : "RGB",
                LOG_COLOR_RESET);
  Serial.print(F(" rmt="));
  Serial.print(g_config.rmtChannel);
  Serial.print(F(" smoothStepMs="));
  Serial.println(g_config.smoothStepMs);
}

static void print_status_one(uint8_t index) {
  StatusLed::LedSnapshot snap;
  const StatusLed::Status st = g_leds.getLedSnapshot(index, &snap);
  if (!st.ok()) {
    LOGE("snapshot failed: %s", st.msg);
    return;
  }
  Serial.print(F("LED "));
  Serial.print(index);
  Serial.print(F(" mode="));
  Serial.print(mode_name(snap.mode));
  Serial.print(F(" preset="));
  Serial.print(preset_name(snap.preset));
  Serial.print(F(" default="));
  Serial.print(preset_name(snap.defaultPreset));
  Serial.print(F(" color="));
  Serial.print(snap.color.r);
  Serial.print(F(","));
  Serial.print(snap.color.g);
  Serial.print(F(","));
  Serial.print(snap.color.b);
  Serial.print(F(" alt="));
  Serial.print(snap.altColor.r);
  Serial.print(F(","));
  Serial.print(snap.altColor.g);
  Serial.print(F(","));
  Serial.print(snap.altColor.b);
  Serial.print(F(" brightness="));
  Serial.print(snap.brightness);
  Serial.print(F(" intensity="));
  Serial.print(snap.intensity);
  if (snap.tempActive) {
    Serial.print(F(" temp="));
    Serial.print(snap.tempRemainingMs);
    Serial.print(F("ms"));
  }
  Serial.println();
}

static void list_modes() {
  Serial.println(F("Modes:"));
  for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); ++i) {
    Serial.print(F("  "));
    Serial.println(kModes[i].name);
  }
}

static void list_presets() {
  Serial.println(F("Presets:"));
  for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
    Serial.print(F("  "));
    Serial.println(kPresets[i].name);
  }
}

static void begin_default() {
  g_config.dataPin = pins::LED_DATA;
  g_config.ledCount = pins::LED_COUNT;
  g_config.colorOrder = StatusLed::ColorOrder::GRB;
  g_config.rmtChannel = 0;
  g_config.smoothStepMs = 20;

  const StatusLed::Status st = g_leds.begin(g_config);
  g_initialized = st.ok();
  if (!st.ok()) {
    LOGE("begin failed: %s", st.msg);
    return;
  }

  if (g_config.ledCount > 0) {
    g_leds.setPreset(0, StatusLed::StatusPreset::Ready);
  }
  if (g_config.ledCount > 1) {
    g_leds.setPreset(1, StatusLed::StatusPreset::Busy);
  }
  if (g_config.ledCount > 2) {
    g_leds.setPreset(2, StatusLed::StatusPreset::Warning);
  }
  LOGI("Started with defaults. dataPin=%d ledCount=%u", g_config.dataPin, g_config.ledCount);
}

static void stress_tick(uint32_t now_ms) {
  if (!g_stress.active || !g_initialized) {
    return;
  }
  if (static_cast<int32_t>(now_ms - g_stress.nextMs) < 0) {
    return;
  }

  g_stress.nextMs = now_ms + g_stress.periodMs;
  const uint8_t count = g_config.ledCount;
  if (count == 0) {
    return;
  }

  const uint8_t index = static_cast<uint8_t>(g_stress.step % count);
  const uint32_t phase = g_stress.step % 6;

  if (phase == 0) {
    const StatusLed::StatusPreset preset = kPresets[(g_stress.step / 6) % (sizeof(kPresets) / sizeof(kPresets[0]))].preset;
    g_leds.setPreset(index, preset);
  } else if (phase == 1) {
    const StatusLed::Mode mode = kModes[(g_stress.step / 6) % (sizeof(kModes) / sizeof(kModes[0]))].mode;
    g_leds.setMode(index, mode);
  } else if (phase == 2) {
    const uint8_t r = static_cast<uint8_t>((g_stress.step * 37) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((g_stress.step * 53) & 0xFF);
    const uint8_t b = static_cast<uint8_t>((g_stress.step * 91) & 0xFF);
    g_leds.setColor(index, StatusLed::RgbColor(r, g, b));
  } else if (phase == 3) {
    const uint8_t level = static_cast<uint8_t>((g_stress.step * 13) & 0xFF);
    g_leds.setBrightness(index, level);
  } else if (phase == 4) {
    g_leds.setTemporaryPreset(index, StatusLed::StatusPreset::Error, 200);
  } else {
    const uint8_t level = static_cast<uint8_t>((g_stress.step * 17) & 0xFF);
    g_leds.setGlobalBrightness(level);
  }

  g_stress.step++;
}

static void handle_command(char* line) {
  char* argv[12];
  const int argc = split_args(line, argv, 12);
  if (argc == 0) {
    return;
  }

  if (strcmp(argv[0], "help") == 0) {
    print_help();
    return;
  }

  if (strcmp(argv[0], "begin") == 0) {
    int pin = pins::LED_DATA;
    uint32_t count = pins::LED_COUNT;
    StatusLed::ColorOrder order = StatusLed::ColorOrder::GRB;
    uint32_t rmt = 0;
    uint32_t smooth = 20;

    if (argc > 1) {
      pin = atoi(argv[1]);
    }
    if (argc > 2) {
      parse_u32(argv[2], &count);
    }
    if (argc > 3) {
      if (strcmp(argv[3], "rgb") == 0) {
        order = StatusLed::ColorOrder::RGB;
      } else if (strcmp(argv[3], "grb") == 0) {
        order = StatusLed::ColorOrder::GRB;
      }
    }
    if (argc > 4) {
      parse_u32(argv[4], &rmt);
    }
    if (argc > 5) {
      parse_u32(argv[5], &smooth);
    }

    g_config.dataPin = pin;
    g_config.ledCount = static_cast<uint8_t>(count);
    g_config.colorOrder = order;
    g_config.rmtChannel = static_cast<uint8_t>(rmt);
    g_config.smoothStepMs = static_cast<uint16_t>(smooth);

    const StatusLed::Status st = g_leds.begin(g_config);
    g_initialized = st.ok();
    if (!st.ok()) {
      LOGE("begin failed: %s", st.msg);
    } else {
      LOGI("Started.");
    }
    return;
  }

  if (strcmp(argv[0], "end") == 0) {
    g_leds.end();
    g_initialized = false;
    LOGI("Stopped.");
    return;
  }

  if (strcmp(argv[0], "status") == 0) {
    if (!g_initialized) {
      LOGI("Not running.");
      Serial.printf("  Driver: %sNOT_RUNNING%s\n", LOG_COLOR_YELLOW, LOG_COLOR_RESET);
      return;
    }
    if (argc > 1) {
      uint32_t idx = 0;
      if (parse_u32(argv[1], &idx)) {
        print_status_one(static_cast<uint8_t>(idx));
      }
    } else {
      for (uint8_t i = 0; i < g_config.ledCount; ++i) {
        print_status_one(i);
      }
    }
    return;
  }

  if (strcmp(argv[0], "config") == 0) {
    print_config();
    return;
  }

  if (strcmp(argv[0], "last") == 0) {
    const StatusLed::Status st = g_leds.getLastStatus();
    Serial.print(F("last: code="));
    Serial.print(st.ok() ? LOG_COLOR_GREEN : LOG_COLOR_RED);
    Serial.print(static_cast<int>(st.code));
    Serial.print(LOG_COLOR_RESET);
    Serial.print(F(" detail="));
    Serial.print(st.detail);
    Serial.print(F(" msg="));
    if (st.msg && st.msg[0] != '\0') {
      Serial.printf("%s%s%s\n", LOG_COLOR_YELLOW, st.msg, LOG_COLOR_RESET);
    } else {
      Serial.println("-");
    }
    return;
  }

  if (strcmp(argv[0], "list_modes") == 0) {
    list_modes();
    return;
  }

  if (strcmp(argv[0], "list_presets") == 0) {
    list_presets();
    return;
  }

  if (strcmp(argv[0], "mode") == 0 && argc >= 3) {
    uint32_t idx = 0;
    if (!parse_u32(argv[1], &idx)) {
      LOGE("invalid index");
      return;
    }
    StatusLed::Mode mode;
    if (!parse_mode(argv[2], &mode)) {
      LOGE("invalid mode");
      return;
    }
    g_leds.setMode(static_cast<uint8_t>(idx), mode);
    return;
  }

  if (strcmp(argv[0], "modep") == 0 && argc >= 9) {
    uint32_t idx = 0;
    if (!parse_u32(argv[1], &idx)) {
      LOGE("invalid index");
      return;
    }
    StatusLed::Mode mode;
    if (!parse_mode(argv[2], &mode)) {
      LOGE("invalid mode");
      return;
    }
    StatusLed::ModeParams params;
    uint32_t tmp = 0;
    parse_u32(argv[3], &tmp);
    params.periodMs = static_cast<uint16_t>(tmp);
    parse_u32(argv[4], &tmp);
    params.onMs = static_cast<uint16_t>(tmp);
    parse_u32(argv[5], &tmp);
    params.riseMs = static_cast<uint16_t>(tmp);
    parse_u32(argv[6], &tmp);
    params.fallMs = static_cast<uint16_t>(tmp);
    parse_u32(argv[7], &tmp);
    params.minLevel = static_cast<uint8_t>(tmp);
    parse_u32(argv[8], &tmp);
    params.maxLevel = static_cast<uint8_t>(tmp);
    g_leds.setMode(static_cast<uint8_t>(idx), mode, params);
    return;
  }

  if (strcmp(argv[0], "color") == 0 && argc >= 5) {
    uint32_t idx = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    if (parse_u32(argv[1], &idx) && parse_u32(argv[2], &r) && parse_u32(argv[3], &g) && parse_u32(argv[4], &b)) {
      g_leds.setColor(static_cast<uint8_t>(idx), StatusLed::RgbColor(r, g, b));
    }
    return;
  }

  if (strcmp(argv[0], "alt") == 0 && argc >= 5) {
    uint32_t idx = 0;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    if (parse_u32(argv[1], &idx) && parse_u32(argv[2], &r) && parse_u32(argv[3], &g) && parse_u32(argv[4], &b)) {
      g_leds.setSecondaryColor(static_cast<uint8_t>(idx), StatusLed::RgbColor(r, g, b));
    }
    return;
  }

  if (strcmp(argv[0], "preset") == 0 && argc >= 3) {
    uint32_t idx = 0;
    StatusLed::StatusPreset preset;
    if (!parse_u32(argv[1], &idx) || !parse_preset(argv[2], &preset)) {
      LOGE("invalid preset");
      return;
    }
    g_leds.setPreset(static_cast<uint8_t>(idx), preset);
    return;
  }

  if (strcmp(argv[0], "default") == 0 && argc >= 3) {
    uint32_t idx = 0;
    StatusLed::StatusPreset preset;
    if (!parse_u32(argv[1], &idx) || !parse_preset(argv[2], &preset)) {
      LOGE("invalid preset");
      return;
    }
    g_leds.setDefaultPreset(static_cast<uint8_t>(idx), preset);
    return;
  }

  if (strcmp(argv[0], "temp") == 0 && argc >= 4) {
    uint32_t idx = 0;
    uint32_t duration = 0;
    StatusLed::StatusPreset preset;
    if (!parse_u32(argv[1], &idx) || !parse_preset(argv[2], &preset) || !parse_u32(argv[3], &duration)) {
      LOGE("invalid temp args");
      return;
    }
    g_leds.setTemporaryPreset(static_cast<uint8_t>(idx), preset, duration);
    return;
  }

  if (strcmp(argv[0], "bright") == 0 && argc >= 3) {
    uint32_t idx = 0;
    uint32_t level = 0;
    if (parse_u32(argv[1], &idx) && parse_u32(argv[2], &level)) {
      g_leds.setBrightness(static_cast<uint8_t>(idx), static_cast<uint8_t>(level));
    }
    return;
  }

  if (strcmp(argv[0], "gbright") == 0 && argc >= 2) {
    uint32_t level = 0;
    if (parse_u32(argv[1], &level)) {
      g_leds.setGlobalBrightness(static_cast<uint8_t>(level));
    }
    return;
  }

  if (strcmp(argv[0], "clear") == 0) {
    g_leds.clear();
    LOGI("Cleared.");
    return;
  }

  if (strcmp(argv[0], "cleartemp") == 0 && argc >= 2) {
    uint32_t idx = 0;
    if (parse_u32(argv[1], &idx)) {
      g_leds.clearTemporary(static_cast<uint8_t>(idx));
    }
    return;
  }

  if (strcmp(argv[0], "allpreset") == 0 && argc >= 2) {
    StatusLed::StatusPreset preset;
    if (!parse_preset(argv[1], &preset)) {
      LOGE("invalid preset");
      return;
    }
    g_leds.setAllPreset(preset);
    return;
  }

  if (strcmp(argv[0], "allmode") == 0 && argc >= 2) {
    StatusLed::Mode mode;
    if (!parse_mode(argv[1], &mode)) {
      LOGE("invalid mode");
      return;
    }
    g_leds.setAllMode(mode);
    return;
  }

  if (strcmp(argv[0], "allcolor") == 0 && argc >= 4) {
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    if (parse_u32(argv[1], &r) && parse_u32(argv[2], &g) && parse_u32(argv[3], &b)) {
      g_leds.setAllColor(StatusLed::RgbColor(r, g, b));
    }
    return;
  }

  if (strcmp(argv[0], "refresh") == 0) {
    g_leds.forceRefresh();
    LOGI("Refresh queued.");
    return;
  }

  if (strcmp(argv[0], "stress") == 0 && argc >= 2) {
    if (strcmp(argv[1], "on") == 0) {
      g_stress.active = true;
      g_stress.step = 0;
      g_stress.nextMs = millis();
      if (argc > 2) {
        uint32_t period = 0;
        if (parse_u32(argv[2], &period)) {
          g_stress.periodMs = static_cast<uint16_t>(period);
        }
      }
      LOGI("Stress test enabled. period=%u ms", g_stress.periodMs);
      Serial.printf("  Stress: %sON%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
    } else if (strcmp(argv[1], "off") == 0) {
      g_stress.active = false;
      LOGI("Stress test disabled.");
      Serial.printf("  Stress: %sOFF%s\n", LOG_COLOR_YELLOW, LOG_COLOR_RESET);
    }
    return;
  }

  LOGE("Unknown command. Type 'help'.");
}

void setup() {
  log_begin(115200);
  print_help();
  begin_default();
  Serial.println(F("Ready. Type a command:"));
}

void loop() {
  const uint32_t now = millis();
  g_leds.tick(now);
  stress_tick(now);

  char* line = read_line();
  if (line) {
    handle_command(line);
  }
}
