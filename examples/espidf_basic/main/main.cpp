/**
 * @file main.cpp
 * @brief Native ESP-IDF entry point for the StatusLed CLI smoke example.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "examples/common/BoardPins.h"
#include "StatusLed/StatusLed.h"
#include "StatusLed/Version.h"

namespace {

constexpr char LOG_COLOR_RESET[] = "\033[0m";
constexpr char LOG_COLOR_RED[] = "\033[31m";
constexpr char LOG_COLOR_GREEN[] = "\033[32m";
constexpr char LOG_COLOR_YELLOW[] = "\033[33m";
constexpr char LOG_COLOR_CYAN[] = "\033[36m";
constexpr size_t LINE_CAPACITY = 128U;
constexpr size_t MAX_ARGS = 12U;

StatusLed::StatusLed g_leds;
StatusLed::Config g_config;
bool g_initialized = false;
char g_line[LINE_CAPACITY] = {};
size_t g_lineLen = 0;

struct StressState {
  bool active = false;
  uint16_t periodMs = 50;
  uint32_t nextMs = 0;
  uint32_t step = 0;
};

StressState g_stress;

struct ModeNameMap {
  const char* name;
  StatusLed::Mode mode;
};

struct PresetNameMap {
  const char* name;
  StatusLed::StatusPreset preset;
};

const ModeNameMap MODES[] = {
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
    {"flickercandle", StatusLed::Mode::FlickerCandle},
    {"glitch", StatusLed::Mode::Glitch},
    {"alternate", StatusLed::Mode::Alternate},
    {"sos", StatusLed::Mode::SOS},
};

const PresetNameMap PRESETS[] = {
    {"off", StatusLed::StatusPreset::Off},
    {"ready", StatusLed::StatusPreset::Ready},
    {"busy", StatusLed::StatusPreset::Busy},
    {"warning", StatusLed::StatusPreset::Warning},
    {"error", StatusLed::StatusPreset::Error},
    {"critical", StatusLed::StatusPreset::Critical},
    {"updating", StatusLed::StatusPreset::Updating},
    {"info", StatusLed::StatusPreset::Info},
    {"maintenance", StatusLed::StatusPreset::Maintenance},
    {"alarmpolice", StatusLed::StatusPreset::AlarmPolice},
    {"hazardamber", StatusLed::StatusPreset::HazardAmber},
    {"success", StatusLed::StatusPreset::Success},
    {"connecting", StatusLed::StatusPreset::Connecting},
    {"lowbattery", StatusLed::StatusPreset::LowBattery},
};

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

TickType_t delayTicks(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0 && ms > 0U) {
    ticks = 1;
  }
  return ticks;
}

const char* backendName(StatusLed::BackendType backend) {
  switch (backend) {
    case StatusLed::BackendType::IdfWs2812:
      return "idf-rmt-legacy";
    case StatusLed::BackendType::Idf5Ws2812:
      return "idf5-rmt";
    case StatusLed::BackendType::NeoPixelBus:
      return "neopixelbus";
    case StatusLed::BackendType::Null:
    default:
      return "null";
  }
}

const char* modeName(StatusLed::Mode mode) {
  for (const auto& item : MODES) {
    if (item.mode == mode) {
      return item.name;
    }
  }
  return "?";
}

const char* presetName(StatusLed::StatusPreset preset) {
  for (const auto& item : PRESETS) {
    if (item.preset == preset) {
      return item.name;
    }
  }
  return "?";
}

bool parseMode(const char* value, StatusLed::Mode* out) {
  if (value == nullptr || out == nullptr) {
    return false;
  }
  for (const auto& item : MODES) {
    if (strcmp(value, item.name) == 0) {
      *out = item.mode;
      return true;
    }
  }
  return false;
}

bool parsePreset(const char* value, StatusLed::StatusPreset* out) {
  if (value == nullptr || out == nullptr) {
    return false;
  }
  for (const auto& item : PRESETS) {
    if (strcmp(value, item.name) == 0) {
      *out = item.preset;
      return true;
    }
  }
  return false;
}

bool parseU32(const char* value, uint32_t* out) {
  if (value == nullptr || value[0] == '\0' || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

bool parseU8(const char* value, uint8_t* out, uint8_t maxValue) {
  uint32_t parsed = 0;
  if (!parseU32(value, &parsed) || parsed > maxValue) {
    return false;
  }
  *out = static_cast<uint8_t>(parsed);
  return true;
}

bool parseU16(const char* value, uint16_t* out, uint16_t minValue, uint16_t maxValue) {
  uint32_t parsed = 0;
  if (!parseU32(value, &parsed) || parsed < minValue || parsed > maxValue) {
    return false;
  }
  *out = static_cast<uint16_t>(parsed);
  return true;
}

int splitArgs(char* line, char* argv[], size_t maxArgs) {
  int argc = 0;
  char* p = line;
  while (*p != '\0' && static_cast<size_t>(argc) < maxArgs) {
    while (*p != '\0' && isspace(static_cast<unsigned char>(*p))) {
      ++p;
    }
    if (*p == '\0') {
      break;
    }
    argv[argc++] = p;
    while (*p != '\0' && !isspace(static_cast<unsigned char>(*p))) {
      ++p;
    }
    if (*p != '\0') {
      *p++ = '\0';
    }
  }
  return argc;
}

char* readLine() {
  while (true) {
    uint8_t value = 0;
    const ssize_t readCount = ::read(STDIN_FILENO, &value, 1U);
    if (readCount != 1) {
      return nullptr;
    }
    const char c = static_cast<char>(value);
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      g_line[g_lineLen] = '\0';
      g_lineLen = 0;
      return g_line;
    }
    if (g_lineLen + 1U < sizeof(g_line)) {
      g_line[g_lineLen++] = c;
    }
  }
}

void printStatus(const StatusLed::Status& st) {
  printf("code=%u detail=%ld msg=%s\n",
         static_cast<unsigned>(st.code),
         static_cast<long>(st.detail),
         (st.msg != nullptr && st.msg[0] != '\0') ? st.msg : "-");
}

bool logStatus(const StatusLed::Status& st) {
  if (!st.ok()) {
    printf("%sERR%s ", LOG_COLOR_RED, LOG_COLOR_RESET);
    printStatus(st);
    return false;
  }
  return true;
}

void printVersion() {
  printf("Version: %s\nBuilt:   %s\nCommit:  %s (%s)\n",
         StatusLed::VERSION,
         StatusLed::BUILD_TIMESTAMP,
         StatusLed::GIT_COMMIT,
         StatusLed::GIT_STATUS);
}

void printHelpItem(const char* command, const char* desc) {
  printf("  %s%-42s%s - %s\n", LOG_COLOR_CYAN, command, LOG_COLOR_RESET, desc);
}

void printHelp() {
  printf("\n%s=== StatusLed Native ESP-IDF CLI ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  printVersion();
  printf("\n%s[Common]%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  printHelpItem("help", "Show this help");
  printHelpItem("version", "Print build/version metadata");
  printf("\n%s[Lifecycle]%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  printHelpItem("begin [pin] [count] [grb|rgb] [rmt] [smooth_ms]", "Initialize driver");
  printHelpItem("end", "Stop driver");
  printHelpItem("stress on [period_ms]", "Enable mixed stress mode");
  printHelpItem("stress off", "Disable stress mode");
  printf("\n%s[Inspect]%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  printHelpItem("info", "Print version, backend, config, and last status");
  printHelpItem("status [index]", "Show LED snapshot");
  printHelpItem("config", "Print active configuration");
  printHelpItem("last", "Print last driver status");
  printHelpItem("list_modes", "List mode names");
  printHelpItem("list_presets", "List preset names");
  printf("\n%s[Control]%s\n", LOG_COLOR_GREEN, LOG_COLOR_RESET);
  printHelpItem("mode <i> <mode>", "Set mode");
  printHelpItem("modep <i> <mode> <period> <on> <rise> <fall> <min> <max>", "Set mode with params");
  printHelpItem("color <i> <r> <g> <b>", "Set primary color");
  printHelpItem("alt <i> <r> <g> <b>", "Set secondary color");
  printHelpItem("preset <i> <preset>", "Set current preset");
  printHelpItem("default <i> <preset>", "Set default preset");
  printHelpItem("temp <i> <preset> <duration_ms>", "Set temporary preset");
  printHelpItem("bright <i> <level>", "Set LED brightness");
  printHelpItem("gbright <level>", "Set global brightness");
  printHelpItem("clear", "Clear all LEDs");
  printHelpItem("cleartemp <i>", "Clear temporary state");
  printHelpItem("allpreset <preset>", "Set preset on all LEDs");
  printHelpItem("allmode <mode>", "Set mode on all LEDs");
  printHelpItem("allcolor <r> <g> <b>", "Set color on all LEDs");
  printHelpItem("refresh", "Force refresh");
  printf("\n");
}

void printConfig() {
  const StatusLed::Config& cfg = g_leds.config();
  printf("dataPin=%d ledCount=%u order=%s rmt=%u smoothStepMs=%u\n",
         cfg.dataPin,
         static_cast<unsigned>(cfg.ledCount),
         cfg.colorOrder == StatusLed::ColorOrder::GRB ? "GRB" : "RGB",
         static_cast<unsigned>(cfg.rmtChannel),
         static_cast<unsigned>(cfg.smoothStepMs));
}

void printInfo() {
  printVersion();
  printf("Backend: %s\n", backendName(StatusLed::kSelectedBackend));
  printf("Initialized: %s\n", g_leds.isInitialized() ? "true" : "false");
  printf("Configured LEDs: %u\n", static_cast<unsigned>(g_leds.ledCount()));
  printConfig();
  printf("Last status: ");
  printStatus(g_leds.lastStatus());
}

void printOneLed(uint8_t index) {
  StatusLed::LedSnapshot snap;
  const StatusLed::Status st = g_leds.getLedSnapshot(index, &snap);
  if (!logStatus(st)) {
    return;
  }
  printf("LED %u mode=%s preset=%s default=%s color=%u,%u,%u alt=%u,%u,%u brightness=%u intensity=%u",
         static_cast<unsigned>(index),
         modeName(snap.mode),
         presetName(snap.preset),
         presetName(snap.defaultPreset),
         static_cast<unsigned>(snap.color.r),
         static_cast<unsigned>(snap.color.g),
         static_cast<unsigned>(snap.color.b),
         static_cast<unsigned>(snap.altColor.r),
         static_cast<unsigned>(snap.altColor.g),
         static_cast<unsigned>(snap.altColor.b),
         static_cast<unsigned>(snap.brightness),
         static_cast<unsigned>(snap.intensity));
  if (snap.tempActive) {
    printf(" temp=%lu ms", static_cast<unsigned long>(snap.tempRemainingMs));
  }
  printf("\n");
}

void listModes() {
  printf("Modes:\n");
  for (const auto& item : MODES) {
    printf("  %s\n", item.name);
  }
}

void listPresets() {
  printf("Presets:\n");
  for (const auto& item : PRESETS) {
    printf("  %s\n", item.name);
  }
}

void beginDefault() {
  g_config.dataPin = pins::LED_DATA;
  g_config.ledCount = pins::LED_COUNT;
  g_config.colorOrder = StatusLed::ColorOrder::GRB;
  g_config.rmtChannel = 0;
  g_config.smoothStepMs = 20;

  const StatusLed::Status st = g_leds.begin(g_config);
  g_initialized = st.ok();
  if (!logStatus(st)) {
    return;
  }
  if (g_config.ledCount > 0U) {
    (void)g_leds.setPreset(0, StatusLed::StatusPreset::Ready);
  }
  if (g_config.ledCount > 1U) {
    (void)g_leds.setPreset(1, StatusLed::StatusPreset::Busy);
  }
  printf("%sOK%s started dataPin=%d ledCount=%u\n",
         LOG_COLOR_GREEN,
         LOG_COLOR_RESET,
         g_config.dataPin,
         static_cast<unsigned>(g_config.ledCount));
}

void stressTick(uint32_t now) {
  if (!g_stress.active || !g_initialized ||
      static_cast<int32_t>(now - g_stress.nextMs) < 0) {
    return;
  }
  g_stress.nextMs = now + g_stress.periodMs;
  if (g_config.ledCount == 0U) {
    return;
  }

  const uint8_t index = static_cast<uint8_t>(g_stress.step % g_config.ledCount);
  const uint32_t phase = g_stress.step % 6U;
  if (phase == 0U) {
    (void)g_leds.setPreset(index, PRESETS[(g_stress.step / 6U) % (sizeof(PRESETS) / sizeof(PRESETS[0]))].preset);
  } else if (phase == 1U) {
    (void)g_leds.setMode(index, MODES[(g_stress.step / 6U) % (sizeof(MODES) / sizeof(MODES[0]))].mode);
  } else if (phase == 2U) {
    (void)g_leds.setColor(index, StatusLed::RgbColor(static_cast<uint8_t>(g_stress.step * 37U),
                                                     static_cast<uint8_t>(g_stress.step * 53U),
                                                     static_cast<uint8_t>(g_stress.step * 91U)));
  } else if (phase == 3U) {
    (void)g_leds.setBrightness(index, static_cast<uint8_t>(g_stress.step * 13U));
  } else if (phase == 4U) {
    (void)g_leds.setTemporaryPreset(index, StatusLed::StatusPreset::Error, 200U);
  } else {
    (void)g_leds.setGlobalBrightness(static_cast<uint8_t>(g_stress.step * 17U));
  }
  ++g_stress.step;
}

void handleCommand(char* line) {
  char* argv[MAX_ARGS] = {};
  const int argc = splitArgs(line, argv, MAX_ARGS);
  if (argc == 0) {
    return;
  }

  if (strcmp(argv[0], "help") == 0) {
    printHelp();
  } else if (strcmp(argv[0], "version") == 0) {
    printVersion();
  } else if (strcmp(argv[0], "info") == 0) {
    printInfo();
  } else if (strcmp(argv[0], "config") == 0) {
    printConfig();
  } else if (strcmp(argv[0], "last") == 0) {
    printStatus(g_leds.lastStatus());
  } else if (strcmp(argv[0], "list_modes") == 0) {
    listModes();
  } else if (strcmp(argv[0], "list_presets") == 0) {
    listPresets();
  } else if (strcmp(argv[0], "begin") == 0) {
    int pin = pins::LED_DATA;
    uint8_t count = pins::LED_COUNT;
    StatusLed::ColorOrder order = StatusLed::ColorOrder::GRB;
    uint8_t rmt = 0;
    uint16_t smooth = 20;
    if (argc > 1) {
      pin = atoi(argv[1]);
    }
    if (argc > 2 && !parseU8(argv[2], &count, StatusLed::StatusLed::kMaxLedCount)) {
      printf("invalid LED count\n");
      return;
    }
    if (argc > 3) {
      order = strcmp(argv[3], "rgb") == 0 ? StatusLed::ColorOrder::RGB : StatusLed::ColorOrder::GRB;
    }
    if (argc > 4 && !parseU8(argv[4], &rmt, 3)) {
      printf("invalid RMT channel\n");
      return;
    }
    if (argc > 5 && !parseU16(argv[5], &smooth, 5, 1000)) {
      printf("invalid smooth_ms\n");
      return;
    }
    g_config.dataPin = pin;
    g_config.ledCount = count;
    g_config.colorOrder = order;
    g_config.rmtChannel = rmt;
    g_config.smoothStepMs = smooth;
    g_initialized = logStatus(g_leds.begin(g_config));
  } else if (strcmp(argv[0], "end") == 0) {
    g_leds.end();
    g_initialized = false;
    printf("stopped\n");
  } else if (strcmp(argv[0], "status") == 0) {
    if (!g_initialized) {
      printf("Driver: %sNOT_RUNNING%s\n", LOG_COLOR_YELLOW, LOG_COLOR_RESET);
      return;
    }
    if (argc > 1) {
      uint32_t idx = 0;
      if (parseU32(argv[1], &idx) && idx <= UINT8_MAX) {
        printOneLed(static_cast<uint8_t>(idx));
      }
    } else {
      for (uint8_t i = 0; i < g_config.ledCount; ++i) {
        printOneLed(i);
      }
    }
  } else if (strcmp(argv[0], "mode") == 0 && argc >= 3) {
    uint32_t idx = 0;
    StatusLed::Mode mode;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parseMode(argv[2], &mode)) {
      (void)logStatus(g_leds.setMode(static_cast<uint8_t>(idx), mode));
    }
  } else if (strcmp(argv[0], "modep") == 0 && argc >= 9) {
    uint32_t idx = 0;
    StatusLed::Mode mode;
    StatusLed::ModeParams params;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parseMode(argv[2], &mode) &&
        parseU16(argv[3], &params.periodMs, 1, UINT16_MAX) &&
        parseU16(argv[4], &params.onMs, 0, UINT16_MAX) &&
        parseU16(argv[5], &params.riseMs, 0, UINT16_MAX) &&
        parseU16(argv[6], &params.fallMs, 0, UINT16_MAX) &&
        parseU8(argv[7], &params.minLevel, 255) &&
        parseU8(argv[8], &params.maxLevel, 255)) {
      (void)logStatus(g_leds.setMode(static_cast<uint8_t>(idx), mode, params));
    }
  } else if (strcmp(argv[0], "color") == 0 && argc >= 5) {
    uint32_t idx = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parseU8(argv[2], &r, 255) &&
        parseU8(argv[3], &g, 255) && parseU8(argv[4], &b, 255)) {
      (void)logStatus(g_leds.setColor(static_cast<uint8_t>(idx), StatusLed::RgbColor(r, g, b)));
    }
  } else if (strcmp(argv[0], "alt") == 0 && argc >= 5) {
    uint32_t idx = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parseU8(argv[2], &r, 255) &&
        parseU8(argv[3], &g, 255) && parseU8(argv[4], &b, 255)) {
      (void)logStatus(g_leds.setSecondaryColor(static_cast<uint8_t>(idx), StatusLed::RgbColor(r, g, b)));
    }
  } else if (strcmp(argv[0], "preset") == 0 && argc >= 3) {
    uint32_t idx = 0;
    StatusLed::StatusPreset preset;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parsePreset(argv[2], &preset)) {
      (void)logStatus(g_leds.setPreset(static_cast<uint8_t>(idx), preset));
    }
  } else if (strcmp(argv[0], "default") == 0 && argc >= 3) {
    uint32_t idx = 0;
    StatusLed::StatusPreset preset;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parsePreset(argv[2], &preset)) {
      (void)logStatus(g_leds.setDefaultPreset(static_cast<uint8_t>(idx), preset));
    }
  } else if (strcmp(argv[0], "temp") == 0 && argc >= 4) {
    uint32_t idx = 0;
    uint32_t duration = 0;
    StatusLed::StatusPreset preset;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parsePreset(argv[2], &preset) &&
        parseU32(argv[3], &duration)) {
      (void)logStatus(g_leds.setTemporaryPreset(static_cast<uint8_t>(idx), preset, duration));
    }
  } else if (strcmp(argv[0], "bright") == 0 && argc >= 3) {
    uint32_t idx = 0;
    uint8_t level = 0;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX && parseU8(argv[2], &level, 255)) {
      (void)logStatus(g_leds.setBrightness(static_cast<uint8_t>(idx), level));
    }
  } else if (strcmp(argv[0], "gbright") == 0 && argc >= 2) {
    uint8_t level = 0;
    if (parseU8(argv[1], &level, 255)) {
      (void)logStatus(g_leds.setGlobalBrightness(level));
    }
  } else if (strcmp(argv[0], "clear") == 0) {
    (void)logStatus(g_leds.clear());
  } else if (strcmp(argv[0], "cleartemp") == 0 && argc >= 2) {
    uint32_t idx = 0;
    if (parseU32(argv[1], &idx) && idx <= UINT8_MAX) {
      (void)logStatus(g_leds.clearTemporary(static_cast<uint8_t>(idx)));
    }
  } else if (strcmp(argv[0], "allpreset") == 0 && argc >= 2) {
    StatusLed::StatusPreset preset;
    if (parsePreset(argv[1], &preset)) {
      (void)logStatus(g_leds.setAllPreset(preset));
    }
  } else if (strcmp(argv[0], "allmode") == 0 && argc >= 2) {
    StatusLed::Mode mode;
    if (parseMode(argv[1], &mode)) {
      (void)logStatus(g_leds.setAllMode(mode));
    }
  } else if (strcmp(argv[0], "allcolor") == 0 && argc >= 4) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (parseU8(argv[1], &r, 255) && parseU8(argv[2], &g, 255) && parseU8(argv[3], &b, 255)) {
      (void)logStatus(g_leds.setAllColor(StatusLed::RgbColor(r, g, b)));
    }
  } else if (strcmp(argv[0], "refresh") == 0) {
    g_leds.forceRefresh();
    printf("refresh queued\n");
  } else if (strcmp(argv[0], "stress") == 0 && argc >= 2) {
    if (strcmp(argv[1], "on") == 0) {
      if (!g_initialized) {
        printf("begin before enabling stress\n");
        return;
      }
      g_stress.active = true;
      g_stress.nextMs = nowMs();
      g_stress.step = 0;
      if (argc > 2) {
        (void)parseU16(argv[2], &g_stress.periodMs, 1, UINT16_MAX);
      }
      printf("Stress: %sON%s period=%u ms\n", LOG_COLOR_GREEN, LOG_COLOR_RESET, static_cast<unsigned>(g_stress.periodMs));
    } else if (strcmp(argv[1], "off") == 0) {
      g_stress.active = false;
      printf("Stress: %sOFF%s\n", LOG_COLOR_YELLOW, LOG_COLOR_RESET);
    }
  } else {
    printf("unknown command; type help\n");
  }
}

void configureConsole() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (flags >= 0) {
    (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
  }
}

}  // namespace

extern "C" void app_main(void) {
  configureConsole();
  printHelp();
  beginDefault();
  printf("Ready. Type a command:\n");

  while (true) {
    const uint32_t now = nowMs();
    g_leds.tick(now);
    stressTick(now);
    char* line = readLine();
    if (line != nullptr) {
      handleCommand(line);
    }
    vTaskDelay(delayTicks(1U));
  }
}
