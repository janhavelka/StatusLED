/**
 * @file StatusLed.cpp
 * @brief Implementation of the StatusLed animation engine.
 *
 * The engine is framework-neutral: it owns per-LED state, computes an
 * intensity for each LED from the current mode and the caller-supplied
 * timestamp, renders the resulting frame, and hands it to the compile-time
 * selected backend only when a pixel value actually changed.
 */

#include "StatusLed/StatusLed.h"
#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#include <stddef.h>

namespace StatusLed {
namespace {

constexpr uint8_t kMaxLeds = StatusLed::kMaxLedCount;
constexpr uint8_t kDimLevel = 48;  // ~19% of full intensity
constexpr uint16_t kMinSmoothStepMs = 5;
constexpr uint16_t kMaxSmoothStepMs = 1000;
constexpr uint16_t kMinPeriodMs = 2;
constexpr uint32_t kMaxDurationMs = 0x7FFFFFFFu;
constexpr int kMaxDataPin = 255;
constexpr uint8_t kMaxRmtChannel = 3;

// FlickerCandle stays in a warm, always-lit band; Glitch drops out entirely.
constexpr uint8_t kFlickerBase = 140;
constexpr uint8_t kFlickerSpan = 100;
constexpr uint8_t kGlitchOffThreshold = 30;
constexpr uint16_t kRandomMinStepMs = 30;
constexpr uint16_t kRandomStepSpanMs = 60;

// 16-bit Galois LFSR (x^16 + x^14 + x^13 + x^11 + 1) used by the random modes.
constexpr uint32_t kLfsrSeed = 0xACE1u;
constexpr uint32_t kLfsrTaps = 0xB400u;

template <typename T, size_t N>
constexpr uint8_t countOf(const T (&)[N]) {
  return static_cast<uint8_t>(N);
}

/// @brief One step of a fixed-timing pattern.
struct PatternStep {
  uint16_t durationMs;
  uint8_t intensity;
  bool useAlt;
};

constexpr PatternStep kPatternDoubleBlink[] = {
  {120, 255, false},
  {120, 0, false},
  {120, 255, false},
  {600, 0, false},
};

constexpr PatternStep kPatternTripleBlink[] = {
  {90, 255, false},
  {90, 0, false},
  {90, 255, false},
  {90, 0, false},
  {90, 255, false},
  {600, 0, false},
};

constexpr PatternStep kPatternHeartbeat[] = {
  {70, 255, false},
  {70, 0, false},
  {70, 200, false},
  {600, 0, false},
};

// Alternate: primary, gap, secondary, gap.
constexpr PatternStep kPatternAlternate[] = {
  {120, 255, false},
  {60, 0, false},
  {120, 255, true},
  {400, 0, false},
};

// Morse SOS: ... --- ... followed by a word gap. Total cycle 3400 ms.
constexpr PatternStep kPatternSOS[] = {
  {100, 255, false}, {100, 0, false},
  {100, 255, false}, {100, 0, false},
  {100, 255, false}, {300, 0, false},
  {300, 255, false}, {100, 0, false},
  {300, 255, false}, {100, 0, false},
  {300, 255, false}, {300, 0, false},
  {100, 255, false}, {100, 0, false},
  {100, 255, false}, {100, 0, false},
  {100, 255, false}, {700, 0, false},
};

/// @brief A fixed-timing pattern: a step table walked one entry per deadline.
struct Pattern {
  const PatternStep* steps;
  uint8_t count;
};

/// @brief Look up the fixed pattern of a mode, or {nullptr, 0} if it has none.
Pattern patternFor(Mode mode) {
  switch (mode) {
    case Mode::DoubleBlink:
      return {kPatternDoubleBlink, countOf(kPatternDoubleBlink)};
    case Mode::TripleBlink:
      return {kPatternTripleBlink, countOf(kPatternTripleBlink)};
    case Mode::Heartbeat:
      return {kPatternHeartbeat, countOf(kPatternHeartbeat)};
    case Mode::Alternate:
      return {kPatternAlternate, countOf(kPatternAlternate)};
    case Mode::SOS:
      return {kPatternSOS, countOf(kPatternSOS)};
    default:
      return {nullptr, 0};
  }
}

struct PresetDef {
  StatusPreset preset;
  Mode mode;
  RgbColor primary;
  RgbColor secondary;
};

constexpr RgbColor kColorOff(0, 0, 0);
constexpr RgbColor kColorGreen(0, 255, 0);
constexpr RgbColor kColorOrange(255, 128, 0);
constexpr RgbColor kColorAmber(255, 180, 0);
constexpr RgbColor kColorRed(255, 0, 0);
constexpr RgbColor kColorCyan(0, 255, 255);
constexpr RgbColor kColorBlue(0, 0, 255);
constexpr RgbColor kColorPurple(128, 0, 255);

constexpr PresetDef kPresets[] = {
  {StatusPreset::Off, Mode::Off, kColorOff, kColorOff},
  {StatusPreset::Ready, Mode::Solid, kColorGreen, kColorOff},
  {StatusPreset::Busy, Mode::PulseSoft, kColorOrange, kColorOff},
  {StatusPreset::Warning, Mode::BlinkSlow, kColorAmber, kColorOff},
  {StatusPreset::Error, Mode::BlinkFast, kColorRed, kColorOff},
  {StatusPreset::Critical, Mode::Strobe, kColorRed, kColorOff},
  {StatusPreset::Updating, Mode::Breathing, kColorCyan, kColorOff},
  {StatusPreset::Info, Mode::Solid, kColorBlue, kColorOff},
  {StatusPreset::Maintenance, Mode::DoubleBlink, kColorPurple, kColorOff},
  {StatusPreset::AlarmPolice, Mode::Alternate, kColorRed, kColorBlue},
  {StatusPreset::HazardAmber, Mode::DoubleBlink, kColorAmber, kColorOff},
  {StatusPreset::Success, Mode::DoubleBlink, kColorGreen, kColorOff},
  {StatusPreset::Connecting, Mode::PulseSoft, kColorBlue, kColorOff},
  {StatusPreset::LowBattery, Mode::Beacon, kColorRed, kColorOff},
};

const PresetDef* findPreset(StatusPreset preset) {
  for (uint8_t i = 0; i < countOf(kPresets); ++i) {
    if (kPresets[i].preset == preset) {
      return &kPresets[i];
    }
  }
  return nullptr;
}

/// @brief Wraparound-safe "now is at or past target" test.
/// @note Treats the nearer half of the 32-bit range as the past, so it stays
///       correct across the ~49.7 day millisecond wrap.
bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<uint32_t>(now - target) < 0x80000000u;
}

/// @brief Next deadline for a repeating step, without long-term drift.
/// @note Advances from the previous deadline so tick latency does not
///       accumulate, but resynchronizes to now when the deadline is already in
///       the past, so a stalled loop never bursts through missed steps.
uint32_t nextDeadline(uint32_t previous, uint32_t now, uint16_t durationMs) {
  const uint32_t scheduled = previous + durationMs;
  return timeReached(now, scheduled) ? (now + durationMs) : scheduled;
}

/// @brief Scale a 0..255 value by a 0..255 factor, rounding to nearest.
uint8_t scale8(uint8_t value, uint8_t scale) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale + 127) / 255);
}

/// @brief Symmetric ease-in/ease-out shaping of a 0..255 position.
uint8_t ease8InOut(uint8_t x) {
  uint8_t y = x;
  if (x & 0x80) {
    y = static_cast<uint8_t>(255 - x);
  }
  uint16_t z = static_cast<uint16_t>(y) * static_cast<uint16_t>(y);
  z = static_cast<uint16_t>(z >> 7);
  const uint8_t out = static_cast<uint8_t>(z > 255 ? 255 : z);
  return (x & 0x80) ? static_cast<uint8_t>(255 - out) : out;
}

/// @brief Linear interpolation from minVal to maxVal at pos/span.
/// @note Handles maxVal < minVal (descending ramps) and clamps to 0..255.
uint8_t lerpU8(uint8_t minVal, uint8_t maxVal, uint16_t pos, uint16_t span) {
  if (span == 0) {
    return maxVal;
  }
  const int32_t delta = static_cast<int32_t>(maxVal) - static_cast<int32_t>(minVal);
  const int32_t out =
      static_cast<int32_t>(minVal) + ((delta * static_cast<int32_t>(pos)) / static_cast<int32_t>(span));
  if (out < 0) {
    return 0;
  }
  if (out > 255) {
    return 255;
  }
  return static_cast<uint8_t>(out);
}

uint8_t safeLedCount(uint8_t count) {
  return (count <= kMaxLeds) ? count : kMaxLeds;
}

bool isValidColorOrder(ColorOrder order) {
  switch (order) {
    case ColorOrder::GRB:
    case ColorOrder::RGB:
      return true;
    default:
      return false;
  }
}

bool isValidMode(Mode mode) {
  switch (mode) {
    case Mode::Off:
    case Mode::Solid:
    case Mode::Dim:
    case Mode::BlinkSlow:
    case Mode::BlinkFast:
    case Mode::DoubleBlink:
    case Mode::TripleBlink:
    case Mode::Beacon:
    case Mode::Strobe:
    case Mode::FadeIn:
    case Mode::FadeOut:
    case Mode::PulseSoft:
    case Mode::PulseSharp:
    case Mode::Breathing:
    case Mode::Heartbeat:
    case Mode::Throb:
    case Mode::FlickerCandle:
    case Mode::Glitch:
    case Mode::Alternate:
    case Mode::SOS:
      return true;
    default:
      return false;
  }
}

/// @brief Force mode parameters into a range the engine can render.
ModeParams sanitizeParams(Mode mode, ModeParams params) {
  if (params.periodMs < kMinPeriodMs) {
    params.periodMs = kMinPeriodMs;
  }
  if (params.onMs > params.periodMs) {
    params.onMs = params.periodMs;
  }
  if (params.maxLevel < params.minLevel) {
    const uint8_t tmp = params.maxLevel;
    params.maxLevel = params.minLevel;
    params.minLevel = tmp;
  }
  if (mode == Mode::FadeIn && params.riseMs == 0) {
    params.riseMs = 1;
  }
  if (mode == Mode::FadeOut && params.fallMs == 0) {
    params.fallMs = 1;
  }
  return params;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Status StatusLed::begin(const Config& config) {
  if (config.dataPin < 0 || config.dataPin > kMaxDataPin) {
    return setLast(Status(Err::INVALID_CONFIG, config.dataPin, "dataPin out of range"));
  }
  if (config.ledCount == 0 || config.ledCount > kMaxLeds) {
    return setLast(Status(Err::INVALID_CONFIG, config.ledCount, "ledCount out of range"));
  }
  if (!isValidColorOrder(config.colorOrder)) {
    return setLast(
        Status(Err::INVALID_CONFIG, static_cast<int32_t>(config.colorOrder), "invalid colorOrder"));
  }
  if (config.rmtChannel > kMaxRmtChannel) {
    return setLast(Status(Err::INVALID_CONFIG, config.rmtChannel, "rmtChannel out of range"));
  }
  if (config.smoothStepMs < kMinSmoothStepMs || config.smoothStepMs > kMaxSmoothStepMs) {
    return setLast(Status(Err::INVALID_CONFIG, config.smoothStepMs, "smoothStepMs out of range"));
  }

  end();

  _lastTickMs = 0;
  _timeSynced = false;
  _frameDirty = false;

  for (uint8_t i = 0; i < kMaxLeds; ++i) {
    _leds[i] = LedState();
    // Give each LED a distinct, non-zero LFSR seed so the random modes do not
    // run in lockstep across the strip.
    const uint32_t seed = (kLfsrSeed ^ (static_cast<uint32_t>(i) * 179u)) & 0xFFFFu;
    _leds[i].lfsr = (seed != 0) ? seed : kLfsrSeed;
    _frame[i] = kColorOff;
  }

  _backend = createBackend();
  if (_backend == nullptr) {
    return setLast(Status(Err::OUT_OF_MEMORY, 0, "backend alloc failed"));
  }

  const Status st = _backend->begin(config);
  if (!st.ok()) {
    destroyBackend(_backend);
    _backend = nullptr;
    return setLast(st);
  }

  // Adopt the configuration only once the backend accepted it.
  _config = config;
  _initialized = true;
  _frameDirty = true;
  return setLast(Ok());
}

void StatusLed::end() {
  if (_backend) {
    _backend->end();
    destroyBackend(_backend);
    _backend = nullptr;
  }
  _initialized = false;
  _timeSynced = false;
}

// ---------------------------------------------------------------------------
// Configuration of a single LED
// ---------------------------------------------------------------------------

ModeParams StatusLed::getModeDefaults(Mode mode) {
  ModeParams params;
  switch (mode) {
    case Mode::BlinkSlow:
      params.periodMs = 1000;
      params.onMs = 500;
      break;
    case Mode::BlinkFast:
      params.periodMs = 250;
      params.onMs = 125;
      break;
    case Mode::Strobe:
      params.periodMs = 100;
      params.onMs = 50;
      break;
    case Mode::Beacon:
      params.periodMs = 4000;
      params.onMs = 80;
      break;
    case Mode::FadeIn:
      params.riseMs = 1000;
      break;
    case Mode::FadeOut:
      params.fallMs = 1000;
      break;
    case Mode::PulseSoft:
      params.periodMs = 2000;
      break;
    case Mode::PulseSharp:
      params.periodMs = 800;
      break;
    case Mode::Breathing:
      params.periodMs = 3000;
      params.minLevel = 20;
      break;
    case Mode::Throb:
      params.periodMs = 4000;
      break;
    default:
      break;
  }
  return params;
}

Status StatusLed::setMode(uint8_t index, Mode mode) {
  return setMode(index, mode, getModeDefaults(mode));
}

Status StatusLed::setMode(uint8_t index, Mode mode, const ModeParams& params) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }
  if (!isValidMode(mode)) {
    return setLast(Status(Err::INVALID_CONFIG, static_cast<int32_t>(mode), "Unknown mode"));
  }

  cancelTemporary(_leds[index]);
  _leds[index].currentPreset = StatusPreset::Off;
  return setLast(setModeInternal(index, mode, params));
}

Status StatusLed::setColor(uint8_t index, const RgbColor& color) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }

  cancelTemporary(_leds[index]);
  _leds[index].currentPreset = StatusPreset::Off;
  return setLast(setColorInternal(index, color, false));
}

Status StatusLed::setSecondaryColor(uint8_t index, const RgbColor& color) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }

  cancelTemporary(_leds[index]);
  _leds[index].currentPreset = StatusPreset::Off;
  return setLast(setColorInternal(index, color, true));
}

Status StatusLed::setPreset(uint8_t index, StatusPreset preset) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }

  cancelTemporary(_leds[index]);
  return setLast(applyPresetInternal(index, preset));
}

Status StatusLed::setDefaultPreset(uint8_t index, StatusPreset preset) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }
  if (findPreset(preset) == nullptr) {
    return setLast(Status(Err::INVALID_CONFIG, static_cast<int32_t>(preset), "Unknown preset"));
  }

  LedState& led = _leds[index];
  led.defaultPreset = preset;

  // Only adopt it right away when the LED is genuinely idle: a running mode or
  // a temporary preset must not be overridden by a default.
  const bool idle = (led.currentPreset == StatusPreset::Off) && (led.mode == Mode::Off) &&
                    !led.tempActive && !led.tempPending;
  if (idle) {
    return setLast(applyPresetInternal(index, preset));
  }
  return setLast(Ok());
}

Status StatusLed::setTemporaryPreset(uint8_t index, StatusPreset preset, uint32_t durationMs) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }
  if (durationMs == 0 || durationMs > kMaxDurationMs) {
    return setLast(Status(Err::INVALID_CONFIG, static_cast<int32_t>(durationMs & 0x7FFFFFFFu),
                          "durationMs out of range"));
  }
  if (findPreset(preset) == nullptr) {
    return setLast(Status(Err::INVALID_CONFIG, static_cast<int32_t>(preset), "Unknown preset"));
  }

  LedState& led = _leds[index];
  led.tempPreset = preset;
  led.tempDurationMs = durationMs;
  led.tempPending = true;
  return setLast(Ok());
}

Status StatusLed::setBrightness(uint8_t index, uint8_t level) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }

  _leds[index].brightness = level;
  refreshLedOutput(index);
  return setLast(Ok());
}

Status StatusLed::setGlobalBrightness(uint8_t level) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }

  _config.globalBrightness = level;
  const uint8_t count = safeLedCount(_config.ledCount);
  for (uint8_t i = 0; i < count; ++i) {
    refreshLedOutput(i);
  }
  return setLast(Ok());
}

Status StatusLed::clear() {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }

  const uint8_t count = safeLedCount(_config.ledCount);
  for (uint8_t i = 0; i < count; ++i) {
    LedState& led = _leds[i];
    cancelTemporary(led);
    led.currentPreset = StatusPreset::Off;
    led.defaultPreset = StatusPreset::Off;
    led.color = kColorOff;
    led.altColor = kColorOff;
    led.brightness = 255;
    led.intensity = 0;
    setModeInternal(i, Mode::Off, ModeParams{});
    refreshLedOutput(i);
  }
  return setLast(Ok());
}

Status StatusLed::clearTemporary(uint8_t index) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }

  LedState& led = _leds[index];
  led.tempPending = false;
  if (led.tempActive) {
    restoreFromTemporary(led, _lastTickMs);
  }
  return setLast(Ok());
}

// ---------------------------------------------------------------------------
// Bulk configuration
// ---------------------------------------------------------------------------

Status StatusLed::setAllPreset(StatusPreset preset) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (findPreset(preset) == nullptr) {
    return setLast(Status(Err::INVALID_CONFIG, static_cast<int32_t>(preset), "Unknown preset"));
  }

  const uint8_t count = safeLedCount(_config.ledCount);
  for (uint8_t i = 0; i < count; ++i) {
    cancelTemporary(_leds[i]);
    applyPresetInternal(i, preset);
  }
  return setLast(Ok());
}

Status StatusLed::setAllMode(Mode mode) {
  return setAllMode(mode, getModeDefaults(mode));
}

Status StatusLed::setAllMode(Mode mode, const ModeParams& params) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!isValidMode(mode)) {
    return setLast(Status(Err::INVALID_CONFIG, static_cast<int32_t>(mode), "Unknown mode"));
  }

  const uint8_t count = safeLedCount(_config.ledCount);
  for (uint8_t i = 0; i < count; ++i) {
    cancelTemporary(_leds[i]);
    _leds[i].currentPreset = StatusPreset::Off;
    setModeInternal(i, mode, params);
  }
  return setLast(Ok());
}

Status StatusLed::setAllColor(const RgbColor& color) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }

  const uint8_t count = safeLedCount(_config.ledCount);
  for (uint8_t i = 0; i < count; ++i) {
    cancelTemporary(_leds[i]);
    _leds[i].currentPreset = StatusPreset::Off;
    setColorInternal(i, color, false);
  }
  return setLast(Ok());
}

void StatusLed::forceRefresh() {
  if (_initialized) {
    _frameDirty = true;
  }
}

Status StatusLed::getLedSnapshot(uint8_t index, LedSnapshot* out) const {
  if (!_initialized) {
    return Status(Err::NOT_INITIALIZED, 0, "begin not called");
  }
  if (out == nullptr) {
    return Status(Err::INVALID_CONFIG, 0, "out must not be null");
  }
  if (!indexValid(index)) {
    return Status(Err::INVALID_CONFIG, index, "index out of range");
  }

  const LedState& led = _leds[index];
  out->mode = led.mode;
  out->preset = led.currentPreset;
  out->defaultPreset = led.defaultPreset;
  out->color = led.color;
  out->altColor = led.altColor;
  out->brightness = led.brightness;
  out->intensity = led.intensity;
  out->tempActive = led.tempActive;
  out->tempPending = led.tempPending;
  out->tempRemainingMs =
      (led.tempActive && !timeReached(_lastTickMs, led.tempUntilMs)) ? (led.tempUntilMs - _lastTickMs) : 0;

  return Ok();
}

// ---------------------------------------------------------------------------
// Internal state transitions
// ---------------------------------------------------------------------------

void StatusLed::cancelTemporary(LedState& led) {
  led.tempActive = false;
  led.tempPending = false;
}

void StatusLed::startMode(LedState& led, uint32_t startMs) {
  led.phase = 0;
  led.useAlt = false;
  led.modeStartMs = startMs;
  led.nextUpdateMs = startMs;
  led.updateScheduled = true;
}

void StatusLed::restoreFromTemporary(LedState& led, uint32_t now_ms) {
  led.tempActive = false;
  led.mode = led.resumeMode;
  led.params = led.resumeParams;
  led.color = led.resumeColor;
  led.altColor = led.resumeAltColor;
  led.currentPreset = led.resumePreset;
  // Rewind the start of the interrupted animation by the time it had already
  // run, so one-shot modes (FadeIn/FadeOut) resume finished instead of
  // replaying, and continuous modes keep their phase.
  startMode(led, now_ms - led.resumeElapsedMs);
  led.nextUpdateMs = now_ms;
}

Status StatusLed::setModeInternal(uint8_t index, Mode mode, const ModeParams& params) {
  LedState& led = _leds[index];
  led.mode = mode;
  led.params = sanitizeParams(mode, params);
  startMode(led, _lastTickMs);
  return Ok();
}

Status StatusLed::setColorInternal(uint8_t index, const RgbColor& color, bool secondary) {
  LedState& led = _leds[index];
  if (secondary) {
    led.altColor = color;
  } else {
    led.color = color;
  }
  refreshLedOutput(index);
  return Ok();
}

Status StatusLed::applyPresetInternal(uint8_t index, StatusPreset preset) {
  const PresetDef* def = findPreset(preset);
  if (def == nullptr) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(preset), "Unknown preset");
  }

  LedState& led = _leds[index];
  led.currentPreset = preset;
  led.color = def->primary;
  led.altColor = def->secondary;
  setModeInternal(index, def->mode, getModeDefaults(def->mode));
  refreshLedOutput(index);
  return Ok();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void StatusLed::refreshLedOutput(uint8_t index) {
  if (index >= kMaxLeds || index >= _config.ledCount) {
    return;
  }
  const LedState& led = _leds[index];
  refreshLedOutput(index, led.intensity, led.useAlt);
}

void StatusLed::refreshLedOutput(uint8_t index, uint8_t intensity, bool useAlt) {
  if (index >= kMaxLeds || index >= _config.ledCount) {
    return;
  }
  const LedState& led = _leds[index];
  const RgbColor base = useAlt ? led.altColor : led.color;

  // intensity (mode) * per-LED brightness * global brightness, then per channel.
  const uint8_t level = scale8(scale8(intensity, led.brightness), _config.globalBrightness);
  const RgbColor out(scale8(base.r, level), scale8(base.g, level), scale8(base.b, level));

  if (_frame[index] != out) {
    _frame[index] = out;
    _frameDirty = true;
  }
}

void StatusLed::updateLed(uint8_t index, uint32_t now_ms) {
  LedState& led = _leds[index];

  // 1. Activate a queued temporary preset, snapshotting what it covers.
  if (led.tempPending) {
    if (!led.tempActive) {
      led.resumeMode = led.mode;
      led.resumeParams = led.params;
      led.resumeColor = led.color;
      led.resumeAltColor = led.altColor;
      led.resumePreset = led.currentPreset;
      led.resumeElapsedMs = now_ms - led.modeStartMs;
    }
    // The preset was validated by setTemporaryPreset(), so this cannot fail.
    applyPresetInternal(index, led.tempPreset);
    led.tempActive = true;
    led.tempPending = false;
    led.tempUntilMs = now_ms + led.tempDurationMs;
  }

  // 2. Expire an active temporary preset.
  if (led.tempActive && timeReached(now_ms, led.tempUntilMs)) {
    restoreFromTemporary(led, now_ms);
  }

  // 3. Advance the animation, but only when a deadline is due.
  if (!led.updateScheduled || !timeReached(now_ms, led.nextUpdateMs)) {
    return;
  }

  const Pattern pattern = patternFor(led.mode);
  if (pattern.steps != nullptr) {
    const PatternStep& step = pattern.steps[led.phase];
    led.intensity = step.intensity;
    led.useAlt = step.useAlt;
    led.phase = static_cast<uint8_t>((led.phase + 1u) % pattern.count);
    led.nextUpdateMs = nextDeadline(led.nextUpdateMs, now_ms, step.durationMs);
    refreshLedOutput(index, led.intensity, led.useAlt);
    return;
  }

  switch (led.mode) {
    case Mode::Off:
      led.intensity = 0;
      led.useAlt = false;
      led.updateScheduled = false;
      break;

    case Mode::Solid:
      led.intensity = 255;
      led.useAlt = false;
      led.updateScheduled = false;
      break;

    case Mode::Dim:
      led.intensity = kDimLevel;
      led.useAlt = false;
      led.updateScheduled = false;
      break;

    case Mode::BlinkSlow:
    case Mode::BlinkFast:
    case Mode::Strobe:
    case Mode::Beacon: {
      // sanitizeParams() guarantees onMs <= periodMs.
      const uint16_t onMs = led.params.onMs;
      const uint16_t offMs = static_cast<uint16_t>(led.params.periodMs - onMs);
      led.useAlt = false;
      if (onMs == 0 || offMs == 0) {
        // Degenerate duty cycle: hold a static level rather than toggling
        // through zero-length phases, which would retransmit every period.
        led.intensity = (onMs == 0) ? 0 : 255;
        led.updateScheduled = false;
        break;
      }
      const bool turnOn = (led.phase == 0);
      led.intensity = turnOn ? 255 : 0;
      led.phase = turnOn ? 1 : 0;
      led.nextUpdateMs = nextDeadline(led.nextUpdateMs, now_ms, turnOn ? onMs : offMs);
    } break;

    case Mode::FadeIn:
    case Mode::FadeOut: {
      const bool rising = (led.mode == Mode::FadeIn);
      const uint16_t span = rising ? led.params.riseMs : led.params.fallMs;
      const uint8_t from = rising ? led.params.minLevel : led.params.maxLevel;
      const uint8_t to = rising ? led.params.maxLevel : led.params.minLevel;
      const uint32_t elapsed = now_ms - led.modeStartMs;
      led.useAlt = false;
      if (elapsed >= span) {
        // One-shot: settle on the end level and stop scheduling work.
        led.intensity = to;
        led.updateScheduled = false;
      } else {
        led.intensity = lerpU8(from, to, static_cast<uint16_t>(elapsed), span);
        led.nextUpdateMs = now_ms + _config.smoothStepMs;
      }
    } break;

    case Mode::PulseSoft:
    case Mode::PulseSharp:
    case Mode::Breathing:
    case Mode::Throb: {
      // Triangle position over the cycle, shaped, then mapped onto the
      // configured min..max window so those bounds are the real output limits.
      const uint16_t period = led.params.periodMs;  // >= kMinPeriodMs
      const uint16_t half = static_cast<uint16_t>(period / 2);
      const uint16_t phase = static_cast<uint16_t>((now_ms - led.modeStartMs) % period);
      const uint8_t pos =
          (phase < half)
              ? lerpU8(0, 255, phase, half)
              : lerpU8(255, 0, static_cast<uint16_t>(phase - half), static_cast<uint16_t>(period - half));

      uint8_t shaped = pos;
      if (led.mode != Mode::PulseSharp) {
        shaped = ease8InOut(pos);
        if (led.mode == Mode::Breathing) {
          shaped = scale8(shaped, shaped);
        }
      }
      led.intensity = lerpU8(led.params.minLevel, led.params.maxLevel, shaped, 255);
      led.useAlt = false;
      led.nextUpdateMs = now_ms + _config.smoothStepMs;
    } break;

    case Mode::FlickerCandle:
    case Mode::Glitch: {
      if (led.lfsr == 0) {
        led.lfsr = kLfsrSeed;
      }
      led.lfsr = (led.lfsr >> 1) ^ (-(static_cast<int32_t>(led.lfsr & 1u)) & kLfsrTaps);
      const uint8_t rand8 = static_cast<uint8_t>(led.lfsr & 0xFFu);
      led.intensity = (led.mode == Mode::FlickerCandle)
                          ? static_cast<uint8_t>(kFlickerBase + (rand8 % kFlickerSpan))
                          : ((rand8 < kGlitchOffThreshold) ? 0 : 255);
      led.useAlt = false;
      led.nextUpdateMs = now_ms + kRandomMinStepMs + (rand8 % kRandomStepSpanMs);
    } break;

    default:
      led.intensity = 0;
      led.useAlt = false;
      led.updateScheduled = false;
      break;
  }

  refreshLedOutput(index, led.intensity, led.useAlt);
}

void StatusLed::tick(uint32_t now_ms) {
  if (!_initialized) {
    return;
  }

  const uint8_t count = safeLedCount(_config.ledCount);

  if (!_timeSynced) {
    // First tick after begin(): adopt the caller's clock rather than assuming
    // it starts near zero.
    for (uint8_t i = 0; i < count; ++i) {
      _leds[i].modeStartMs = now_ms;
      _leds[i].nextUpdateMs = now_ms;
    }
    _timeSynced = true;
  }

  _lastTickMs = now_ms;

  for (uint8_t i = 0; i < count; ++i) {
    updateLed(i, now_ms);
  }

  if (_frameDirty && _backend != nullptr && _backend->canShow()) {
    const Status st = _backend->show(_frame, count, _config.colorOrder);
    if (st.ok()) {
      _frameDirty = false;
    } else if (st.code != Err::RESOURCE_BUSY) {
      // Busy is normal back-pressure: keep the frame dirty and retry next
      // tick. Anything else is reported through lastStatus().
      _lastStatus = st;
    }
  }
}

}  // namespace StatusLed
