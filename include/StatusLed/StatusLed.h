/**
 * @file StatusLed.h
 * @brief Status LED subsystem for WS2812-class LEDs.
 *
 * Provides a non-blocking, cooperative status LED engine with begin/tick/end
 * lifecycle. Modes define temporal intensity behavior; colors are configured
 * separately.
 */

#pragma once

#include <stdint.h>

#include "StatusLed/BackendConfig.h"
#include "StatusLed/Config.h"
#include "StatusLed/Status.h"

namespace StatusLed {

struct BackendBase;

/**
 * @brief Simple RGB color.
 */
struct RgbColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  constexpr RgbColor() = default;
  constexpr RgbColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}

  constexpr bool operator==(const RgbColor& other) const {
    return r == other.r && g == other.g && b == other.b;
  }
  constexpr bool operator!=(const RgbColor& other) const { return !(*this == other); }
};

/**
 * @brief Temporal intensity modes (color is configured separately).
 */
enum class Mode : uint8_t {
  Off = 0,
  Solid,
  Dim,
  BlinkSlow,
  BlinkFast,
  DoubleBlink,
  TripleBlink,
  Beacon,
  Strobe,
  FadeIn,
  FadeOut,
  PulseSoft,
  PulseSharp,
  Breathing,
  Heartbeat,
  Throb,
  FlickerCandle,
  Glitch,
  Alternate
};

/**
 * @brief Semantic status presets.
 *
 * Presets bundle mode + color definitions for production statuses.
 */
enum class StatusPreset : uint8_t {
  Off = 0,
  Ready,
  Busy,
  Warning,
  Error,
  Critical,
  Updating,
  Info,
  Maintenance,
  AlarmPolice,
  HazardAmber
};

/**
 * @brief Optional mode parameters for customization.
 */
struct ModeParams {
  /// @brief Total period for repeating modes (ms).
  uint16_t periodMs = 1000;

  /// @brief On-time for simple blink modes (ms).
  uint16_t onMs = 500;

  /// @brief Rise time for fade-in modes (ms).
  uint16_t riseMs = 800;

  /// @brief Fall time for fade-out modes (ms).
  uint16_t fallMs = 800;

  /// @brief Minimum intensity for smooth modes (0..255).
  uint8_t minLevel = 0;

  /// @brief Maximum intensity for smooth modes (0..255).
  uint8_t maxLevel = 255;
};

/**
 * @brief Snapshot of a single LED runtime state.
 */
struct LedSnapshot {
  Mode mode = Mode::Off;
  StatusPreset preset = StatusPreset::Off;
  StatusPreset defaultPreset = StatusPreset::Off;
  RgbColor color{};
  RgbColor altColor{};
  uint8_t brightness = 255;
  uint8_t intensity = 0;
  bool tempActive = false;
  uint32_t tempRemainingMs = 0;
};

/**
 * @brief Main status LED controller.
 *
 * Usage:
 * @code
 * StatusLed::StatusLed leds;
 * StatusLed::Config cfg;
 * cfg.dataPin = 48;
 * cfg.ledCount = 3;
 * auto st = leds.begin(cfg);
 * if (!st.ok()) { // handle error }
 *
 * leds.setPreset(0, StatusLed::StatusPreset::Ready);
 *
 * void loop() {
 *   leds.tick(millis());
 * }
 * @endcode
 *
 * @note This class is not thread-safe. Call all methods from the same
 *       task/thread (typically Arduino loop()).
 * @note Do not call from ISRs.
 */
class StatusLed {
 public:
  /// @brief Maximum number of LEDs supported by the library.
  static constexpr uint8_t kMaxLedCount = 10;

  /// @brief Destructor releases backend resources.
  ~StatusLed() { end(); }

  /**
   * @brief Initialize the library with the given configuration.
   *
   * Must be called before tick(). Can be called again after end() to
   * reinitialize with different settings.
   *
   * @param config Configuration struct with pins and parameters.
   * @return Status Ok on success, or error with details.
   *
   * @note Allocates backend resources. Call end() to release.
   */
  Status begin(const Config& config);

  /**
   * @brief Stop the library and release resources.
   *
   * Safe to call multiple times. After end(), isInitialized() returns false.
   * Call begin() to restart.
   */
  void end();

  /**
   * @brief Cooperative update function. Call from loop().
   *
   * Performs bounded, non-blocking updates. Only transmits when output
   * changes.
   *
   * @param now_ms Current time in milliseconds (typically from millis()).
   */
  void tick(uint32_t now_ms);

  /**
   * @brief Set mode for a given LED using default parameters.
   * @param index LED index (0..ledCount-1).
   * @param mode Desired mode.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setMode(uint8_t index, Mode mode);

  /**
   * @brief Set mode for a given LED with custom parameters.
   * @param index LED index (0..ledCount-1).
   * @param mode Desired mode.
   * @param params Custom parameters for the mode.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setMode(uint8_t index, Mode mode, const ModeParams& params);

  /**
   * @brief Set primary color for a given LED.
   * @param index LED index (0..ledCount-1).
   * @param color RGB color.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setColor(uint8_t index, const RgbColor& color);

  /**
   * @brief Set secondary (alternate) color for composite modes.
   * @param index LED index (0..ledCount-1).
   * @param color RGB color.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setSecondaryColor(uint8_t index, const RgbColor& color);

  /**
   * @brief Set semantic preset for a given LED.
   * @param index LED index (0..ledCount-1).
   * @param preset Preset definition.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setPreset(uint8_t index, StatusPreset preset);

  /**
   * @brief Set default preset for a given LED.
   * @param index LED index (0..ledCount-1).
   * @param preset Default preset definition.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setDefaultPreset(uint8_t index, StatusPreset preset);

  /**
   * @brief Set temporary preset for a given LED, then revert.
   * @param index LED index (0..ledCount-1).
   * @param preset Temporary preset.
   * @param durationMs Duration in milliseconds.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   * @note Temporary preset activates on the next tick() call.
   */
  Status setTemporaryPreset(uint8_t index, StatusPreset preset, uint32_t durationMs);

  /**
   * @brief Set per-LED brightness (0..255).
   * @param index LED index (0..ledCount-1).
   * @param level Brightness level.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status setBrightness(uint8_t index, uint8_t level);

  /**
   * @brief Set global brightness (0..255) for all LEDs.
   * @param level Brightness level.
   * @return Status Ok on success.
   */
  Status setGlobalBrightness(uint8_t level);

  /**
   * @brief Get a snapshot of LED state.
   * @param index LED index (0..ledCount-1).
   * @param out Snapshot output.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status getLedSnapshot(uint8_t index, LedSnapshot* out) const;

  /**
   * @brief Get default parameters for a given mode.
   * @param mode Mode to query.
   * @return ModeParams defaults for that mode.
   */
  static ModeParams getModeDefaults(Mode mode);

  /// @brief Check if library is currently initialized.
  bool isInitialized() const { return _initialized; }

  /// @brief Get current configuration.
  const Config& getConfig() const { return _config; }

  /// @brief Get last error status recorded by the library.
  Status getLastStatus() const { return _lastStatus; }

  /// @brief Get number of LEDs configured.
  uint8_t ledCount() const { return _config.ledCount; }

 private:
  struct LedState {
    Mode mode = Mode::Off;
    ModeParams params{};
    RgbColor color{};
    RgbColor altColor{};
    uint8_t brightness = 255;
    uint8_t intensity = 0;
    uint8_t phase = 0;
    bool useAlt = false;
    uint32_t nextUpdateMs = 0;
    uint32_t phaseEndMs = 0;
    uint32_t modeStartMs = 0;
    StatusPreset currentPreset = StatusPreset::Off;
    StatusPreset defaultPreset = StatusPreset::Off;

    bool tempActive = false;
    bool tempPending = false;
    StatusPreset tempPreset = StatusPreset::Off;
    uint32_t tempUntilMs = 0;
    uint32_t tempDurationMs = 0;

    Mode resumeMode = Mode::Off;
    ModeParams resumeParams{};
    RgbColor resumeColor{};
    RgbColor resumeAltColor{};
    uint8_t resumeBrightness = 255;
    StatusPreset resumePreset = StatusPreset::Off;

    uint32_t lfsr = 0xABCDEu;
  };

  Status setModeInternal(uint8_t index, Mode mode, const ModeParams& params);
  Status setColorInternal(uint8_t index, const RgbColor& color, bool secondary);
  Status applyPresetInternal(uint8_t index, StatusPreset preset);
  void updateLed(uint8_t index, uint32_t now_ms);
  void refreshLedOutput(uint8_t index, uint8_t intensity, bool useAlt);
  void refreshLedOutput(uint8_t index);

  bool indexValid(uint8_t index) const { return index < _config.ledCount; }
  Status setLast(const Status& st) {
    _lastStatus = st;
    return st;
  }

  Config _config{};
  bool _initialized = false;
  Status _lastStatus{};
  uint32_t _lastTickMs = 0;
  bool _frameDirty = false;

  LedState _leds[kMaxLedCount]{};
  RgbColor _frame[kMaxLedCount]{};
  BackendBase* _backend = nullptr;
};

}  // namespace StatusLed
