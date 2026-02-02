/**
 * @file StatusLed.cpp
 * @brief Implementation of StatusLed.
 */

#include "StatusLed/StatusLed.h"
#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#include <stddef.h>

namespace StatusLed {
namespace {

static constexpr uint8_t kMaxLeds = StatusLed::kMaxLedCount;
static constexpr uint8_t kDimLevel = 48;  // ~19% brightness
static constexpr uint32_t kNever = 0xFFFFFFFFu;
static constexpr uint16_t kMinSmoothStepMs = 5;
static constexpr uint16_t kMaxSmoothStepMs = 1000;

struct PatternStep {
  uint16_t durationMs;
  uint8_t intensity;
  bool useAlt;
};

static constexpr PatternStep kPatternDoubleBlink[] = {
  {120, 255, false},
  {120, 0, false},
  {120, 255, false},
  {600, 0, false},
};

static constexpr PatternStep kPatternTripleBlink[] = {
  {90, 255, false},
  {90, 0, false},
  {90, 255, false},
  {90, 0, false},
  {90, 255, false},
  {600, 0, false},
};

static constexpr PatternStep kPatternBeacon[] = {
  {80, 255, false},
  {3920, 0, false},
};

static constexpr PatternStep kPatternStrobe[] = {
  {50, 255, false},
  {50, 0, false},
};

static constexpr PatternStep kPatternHeartbeat[] = {
  {70, 255, false},
  {70, 0, false},
  {70, 200, false},
  {600, 0, false},
};

static constexpr PatternStep kPatternPolice[] = {
  {120, 255, false},
  {60, 0, false},
  {120, 255, true},
  {400, 0, false},
};

struct PresetDef {
  StatusPreset preset;
  Mode mode;
  RgbColor primary;
  RgbColor secondary;
  bool useSecondary;
};

static constexpr RgbColor kColorOff(0, 0, 0);
static constexpr RgbColor kColorGreen(0, 255, 0);
static constexpr RgbColor kColorOrange(255, 128, 0);
static constexpr RgbColor kColorAmber(255, 180, 0);
static constexpr RgbColor kColorRed(255, 0, 0);
static constexpr RgbColor kColorCyan(0, 255, 255);
static constexpr RgbColor kColorBlue(0, 0, 255);
static constexpr RgbColor kColorPurple(128, 0, 255);

static constexpr PresetDef kPresets[] = {
  {StatusPreset::Off, Mode::Off, kColorOff, kColorOff, false},
  {StatusPreset::Ready, Mode::Solid, kColorGreen, kColorOff, false},
  {StatusPreset::Busy, Mode::PulseSoft, kColorOrange, kColorOff, false},
  {StatusPreset::Warning, Mode::BlinkSlow, kColorAmber, kColorOff, false},
  {StatusPreset::Error, Mode::BlinkFast, kColorRed, kColorOff, false},
  {StatusPreset::Critical, Mode::Strobe, kColorRed, kColorOff, false},
  {StatusPreset::Updating, Mode::Breathing, kColorCyan, kColorOff, false},
  {StatusPreset::Info, Mode::Solid, kColorBlue, kColorOff, false},
  {StatusPreset::Maintenance, Mode::DoubleBlink, kColorPurple, kColorOff, false},
  {StatusPreset::AlarmPolice, Mode::Alternate, kColorRed, kColorBlue, true},
  {StatusPreset::HazardAmber, Mode::DoubleBlink, kColorAmber, kColorOff, false},
};

static const PresetDef* findPreset(StatusPreset preset) {
  for (size_t i = 0; i < (sizeof(kPresets) / sizeof(kPresets[0])); ++i) {
    if (kPresets[i].preset == preset) {
      return &kPresets[i];
    }
  }
  return nullptr;
}

static bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

static uint8_t scale8(uint8_t value, uint8_t scale) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale + 127) / 255);
}

static uint8_t ease8InOut(uint8_t x) {
  uint8_t y = x;
  if (x & 0x80) {
    y = 255 - x;
  }
  uint16_t z = static_cast<uint16_t>(y) * static_cast<uint16_t>(y);
  z = z >> 7;
  uint8_t out = static_cast<uint8_t>(z > 255 ? 255 : z);
  return (x & 0x80) ? static_cast<uint8_t>(255 - out) : out;
}

static uint8_t lerpU8(uint8_t minVal, uint8_t maxVal, uint16_t pos, uint16_t span) {
  if (span == 0) {
    return maxVal;
  }
  const uint16_t range = static_cast<uint16_t>(maxVal - minVal);
  const uint16_t scaled = static_cast<uint16_t>((static_cast<uint32_t>(range) * pos) / span);
  return static_cast<uint8_t>(minVal + scaled);
}

static ModeParams sanitizeParams(Mode mode, ModeParams params) {
  if (params.periodMs < 2) {
    params.periodMs = 2;
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

static bool isValidMode(Mode mode) {
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
      return true;
    default:
      return false;
  }
}

}  // namespace

Status StatusLed::begin(const Config& config) {
  if (config.dataPin < 0) {
    return setLast(Status(Err::INVALID_CONFIG, 0, "dataPin must be >= 0"));
  }
  if (config.ledCount == 0 || config.ledCount > kMaxLeds) {
    return setLast(Status(Err::INVALID_CONFIG, config.ledCount, "ledCount out of range"));
  }
  if (config.rmtChannel > 3) {
    return setLast(Status(Err::INVALID_CONFIG, config.rmtChannel, "rmtChannel out of range"));
  }
  if (config.smoothStepMs < kMinSmoothStepMs || config.smoothStepMs > kMaxSmoothStepMs) {
    return setLast(Status(Err::INVALID_CONFIG, config.smoothStepMs, "smoothStepMs out of range"));
  }

  end();

  _config = config;
  _lastTickMs = 0;
  _frameDirty = false;

  for (uint8_t i = 0; i < kMaxLeds; ++i) {
    _leds[i] = LedState();
    _leds[i].lfsr = 0xABCDEu ^ (static_cast<uint32_t>(i) * 7919u);
    _frame[i] = kColorOff;
  }

  _backend = createBackend();
  if (_backend == nullptr) {
    return setLast(Status(Err::OUT_OF_MEMORY, 0, "backend alloc failed"));
  }

  const Status st = _backend->begin(_config);
  if (!st.ok()) {
    destroyBackend(_backend);
    _backend = nullptr;
    return setLast(st);
  }

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
}

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
    case Mode::FadeIn:
      params.riseMs = 1000;
      break;
    case Mode::FadeOut:
      params.fallMs = 1000;
      break;
    case Mode::PulseSoft:
      params.periodMs = 2000;
      params.minLevel = 0;
      params.maxLevel = 255;
      break;
    case Mode::PulseSharp:
      params.periodMs = 800;
      params.minLevel = 0;
      params.maxLevel = 255;
      break;
    case Mode::Breathing:
      params.periodMs = 3000;
      params.minLevel = 20;
      params.maxLevel = 255;
      break;
    case Mode::Throb:
      params.periodMs = 4000;
      params.minLevel = 0;
      params.maxLevel = 255;
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
  _leds[index].currentPreset = StatusPreset::Off;
  const Status st = setColorInternal(index, color, false);
  return setLast(st);
}

Status StatusLed::setSecondaryColor(uint8_t index, const RgbColor& color) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }
  _leds[index].currentPreset = StatusPreset::Off;
  const Status st = setColorInternal(index, color, true);
  return setLast(st);
}

Status StatusLed::setPreset(uint8_t index, StatusPreset preset) {
  if (!_initialized) {
    return setLast(Status(Err::NOT_INITIALIZED, 0, "begin not called"));
  }
  if (!indexValid(index)) {
    return setLast(Status(Err::INVALID_CONFIG, index, "index out of range"));
  }

  _leds[index].tempActive = false;
  _leds[index].tempPending = false;

  const Status st = applyPresetInternal(index, preset);
  return setLast(st);
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

  _leds[index].defaultPreset = preset;

  if (_leds[index].currentPreset == StatusPreset::Off && _leds[index].mode == Mode::Off) {
    const Status st = applyPresetInternal(index, preset);
    return setLast(st);
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
  if (durationMs == 0) {
    return setLast(Status(Err::INVALID_CONFIG, 0, "durationMs must be > 0"));
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
  for (uint8_t i = 0; i < _config.ledCount; ++i) {
    refreshLedOutput(i);
  }
  return setLast(Ok());
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
  if (led.tempActive && timeReached(_lastTickMs, led.tempUntilMs)) {
    out->tempRemainingMs = 0;
  } else if (led.tempActive) {
    out->tempRemainingMs = led.tempUntilMs - _lastTickMs;
  } else {
    out->tempRemainingMs = 0;
  }

  return Ok();
}

Status StatusLed::setModeInternal(uint8_t index, Mode mode, const ModeParams& params) {
  LedState& led = _leds[index];
  led.mode = mode;
  led.params = sanitizeParams(mode, params);
  led.phase = 0;
  led.useAlt = false;
  led.modeStartMs = _lastTickMs;
  led.nextUpdateMs = _lastTickMs;
  led.phaseEndMs = _lastTickMs;
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
  led.useAlt = false;
  const ModeParams defaults = getModeDefaults(def->mode);
  setModeInternal(index, def->mode, defaults);

  if (def->useSecondary) {
    led.altColor = def->secondary;
  }
  refreshLedOutput(index);
  return Ok();
}

void StatusLed::refreshLedOutput(uint8_t index) {
  const LedState& led = _leds[index];
  refreshLedOutput(index, led.intensity, led.useAlt);
}

void StatusLed::refreshLedOutput(uint8_t index, uint8_t intensity, bool useAlt) {
  const LedState& led = _leds[index];
  const RgbColor base = useAlt ? led.altColor : led.color;

  const uint8_t scaled1 = scale8(intensity, led.brightness);
  const uint8_t scaled2 = scale8(scaled1, _config.globalBrightness);

  const RgbColor out(
      scale8(base.r, scaled2),
      scale8(base.g, scaled2),
      scale8(base.b, scaled2));

  if (_frame[index] != out) {
    _frame[index] = out;
    _frameDirty = true;
  }
}

void StatusLed::updateLed(uint8_t index, uint32_t now_ms) {
  LedState& led = _leds[index];

  if (led.tempPending) {
    if (!led.tempActive) {
      led.resumeMode = led.mode;
      led.resumeParams = led.params;
      led.resumeColor = led.color;
      led.resumeAltColor = led.altColor;
      led.resumeBrightness = led.brightness;
      led.resumePreset = led.currentPreset;
    }
    applyPresetInternal(index, led.tempPreset);
    led.tempActive = true;
    led.tempPending = false;
    led.tempUntilMs = now_ms + led.tempDurationMs;
  }

  if (led.tempActive && timeReached(now_ms, led.tempUntilMs)) {
    led.tempActive = false;
    led.mode = led.resumeMode;
    led.params = led.resumeParams;
    led.color = led.resumeColor;
    led.altColor = led.resumeAltColor;
    led.brightness = led.resumeBrightness;
    led.currentPreset = led.resumePreset;
    led.phase = 0;
    led.useAlt = false;
    led.modeStartMs = now_ms;
    led.nextUpdateMs = now_ms;
    led.phaseEndMs = now_ms;
    refreshLedOutput(index);
  }

  if (led.nextUpdateMs == kNever) {
    return;
  }
  if (!timeReached(now_ms, led.nextUpdateMs)) {
    return;
  }

  switch (led.mode) {
    case Mode::Off:
      led.intensity = 0;
      led.useAlt = false;
      led.nextUpdateMs = kNever;
      break;
    case Mode::Solid:
      led.intensity = 255;
      led.useAlt = false;
      led.nextUpdateMs = kNever;
      break;
    case Mode::Dim:
      led.intensity = kDimLevel;
      led.useAlt = false;
      led.nextUpdateMs = kNever;
      break;
    case Mode::BlinkSlow:
    case Mode::BlinkFast: {
      const uint16_t onMs = led.params.onMs;
      const uint16_t periodMs = led.params.periodMs;
      const uint16_t offMs = (periodMs > onMs) ? static_cast<uint16_t>(periodMs - onMs) : 0;
      if (led.phase == 0) {
        led.phase = 1;
        led.intensity = 255;
        led.useAlt = false;
        led.phaseEndMs = now_ms + onMs;
      } else {
        led.phase = 0;
        led.intensity = 0;
        led.useAlt = false;
        led.phaseEndMs = now_ms + offMs;
      }
      led.nextUpdateMs = led.phaseEndMs;
    } break;
    case Mode::DoubleBlink: {
      const PatternStep& step = kPatternDoubleBlink[led.phase % (sizeof(kPatternDoubleBlink) / sizeof(kPatternDoubleBlink[0]))];
      led.intensity = step.intensity;
      led.useAlt = step.useAlt;
      led.phase = static_cast<uint8_t>(led.phase + 1);
      led.nextUpdateMs = now_ms + step.durationMs;
    } break;
    case Mode::TripleBlink: {
      const PatternStep& step = kPatternTripleBlink[led.phase % (sizeof(kPatternTripleBlink) / sizeof(kPatternTripleBlink[0]))];
      led.intensity = step.intensity;
      led.useAlt = step.useAlt;
      led.phase = static_cast<uint8_t>(led.phase + 1);
      led.nextUpdateMs = now_ms + step.durationMs;
    } break;
    case Mode::Beacon: {
      const PatternStep& step = kPatternBeacon[led.phase % (sizeof(kPatternBeacon) / sizeof(kPatternBeacon[0]))];
      led.intensity = step.intensity;
      led.useAlt = step.useAlt;
      led.phase = static_cast<uint8_t>(led.phase + 1);
      led.nextUpdateMs = now_ms + step.durationMs;
    } break;
    case Mode::Strobe: {
      const PatternStep& step = kPatternStrobe[led.phase % (sizeof(kPatternStrobe) / sizeof(kPatternStrobe[0]))];
      led.intensity = step.intensity;
      led.useAlt = step.useAlt;
      led.phase = static_cast<uint8_t>(led.phase + 1);
      led.nextUpdateMs = now_ms + step.durationMs;
    } break;
    case Mode::Heartbeat: {
      const PatternStep& step = kPatternHeartbeat[led.phase % (sizeof(kPatternHeartbeat) / sizeof(kPatternHeartbeat[0]))];
      led.intensity = step.intensity;
      led.useAlt = step.useAlt;
      led.phase = static_cast<uint8_t>(led.phase + 1);
      led.nextUpdateMs = now_ms + step.durationMs;
    } break;
    case Mode::Alternate: {
      const PatternStep& step = kPatternPolice[led.phase % (sizeof(kPatternPolice) / sizeof(kPatternPolice[0]))];
      led.intensity = step.intensity;
      led.useAlt = step.useAlt;
      led.phase = static_cast<uint8_t>(led.phase + 1);
      led.nextUpdateMs = now_ms + step.durationMs;
    } break;
    case Mode::FadeIn: {
      const uint32_t elapsed = now_ms - led.modeStartMs;
      if (elapsed >= led.params.riseMs) {
        led.intensity = 255;
        led.useAlt = false;
        led.nextUpdateMs = kNever;
      } else {
        led.intensity = lerpU8(0, 255, static_cast<uint16_t>(elapsed), led.params.riseMs);
        led.useAlt = false;
        led.nextUpdateMs = now_ms + _config.smoothStepMs;
      }
    } break;
    case Mode::FadeOut: {
      const uint32_t elapsed = now_ms - led.modeStartMs;
      if (elapsed >= led.params.fallMs) {
        led.intensity = 0;
        led.useAlt = false;
        led.nextUpdateMs = kNever;
      } else {
        led.intensity = lerpU8(255, 0, static_cast<uint16_t>(elapsed), led.params.fallMs);
        led.useAlt = false;
        led.nextUpdateMs = now_ms + _config.smoothStepMs;
      }
    } break;
    case Mode::PulseSoft:
    case Mode::PulseSharp:
    case Mode::Breathing:
    case Mode::Throb: {
      const uint16_t period = (led.params.periodMs > 0) ? led.params.periodMs : 1;
      const uint16_t phase = static_cast<uint16_t>(now_ms % period);
      const uint16_t half = period / 2;
      uint8_t raw = 0;
      if (phase < half) {
        raw = lerpU8(led.params.minLevel, led.params.maxLevel, phase, half);
      } else {
        raw = lerpU8(led.params.maxLevel, led.params.minLevel, static_cast<uint16_t>(phase - half), half);
      }
      uint8_t shaped = raw;
      if (led.mode == Mode::PulseSoft || led.mode == Mode::Breathing || led.mode == Mode::Throb) {
        shaped = ease8InOut(raw);
        if (led.mode == Mode::Breathing) {
          shaped = scale8(shaped, shaped);
        }
      }
      led.intensity = shaped;
      led.useAlt = false;
      led.nextUpdateMs = now_ms + _config.smoothStepMs;
    } break;
    case Mode::FlickerCandle:
    case Mode::Glitch: {
      led.lfsr = (led.lfsr >> 1) ^ (-(static_cast<int32_t>(led.lfsr & 1u)) & 0xB400u);
      const uint8_t rand8 = static_cast<uint8_t>(led.lfsr & 0xFFu);
      if (led.mode == Mode::FlickerCandle) {
        const uint8_t base = 140;
        const uint8_t span = 100;
        led.intensity = static_cast<uint8_t>(base + (rand8 % span));
      } else {
        led.intensity = (rand8 < 30) ? 0 : 255;
      }
      led.useAlt = false;
      const uint16_t jitter = static_cast<uint16_t>(30 + (rand8 % 60));
      led.nextUpdateMs = now_ms + jitter;
    } break;
    default:
      led.intensity = 0;
      led.nextUpdateMs = kNever;
      break;
  }

  refreshLedOutput(index, led.intensity, led.useAlt);
}

void StatusLed::tick(uint32_t now_ms) {
  if (!_initialized) {
    return;
  }

  _lastTickMs = now_ms;

  for (uint8_t i = 0; i < _config.ledCount; ++i) {
    updateLed(i, now_ms);
  }

  if (_frameDirty && _backend && _backend->canShow()) {
    const Status st = _backend->show(_frame, _config.ledCount, _config.colorOrder);
    if (st.ok()) {
      _frameDirty = false;
    } else if (st.code == Err::RESOURCE_BUSY) {
      // Keep dirty and try again next tick
    } else {
      _lastStatus = st;
    }
  }
}

}  // namespace StatusLed
