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

#ifndef STATUSLED_MAX_LED_COUNT
/// @brief Build-wide LED capacity (1..255); must match in every translation unit.
#define STATUSLED_MAX_LED_COUNT 10
#endif

static_assert(STATUSLED_MAX_LED_COUNT >= 1 && STATUSLED_MAX_LED_COUNT <= 255,
              "STATUSLED_MAX_LED_COUNT must be 1..255");

namespace StatusLed {

struct BackendBase;

#if STATUSLED_BACKEND_NULL && defined(STATUSLED_TEST)
namespace NullBackendTest {
struct EngineAccess;
}
#endif

/**
 * @brief Simple RGB color.
 */
struct RgbColor {
  /// @brief Red channel intensity (0..255).
  uint8_t r = 0;
  /// @brief Green channel intensity (0..255).
  uint8_t g = 0;
  /// @brief Blue channel intensity (0..255).
  uint8_t b = 0;

  /// @brief Construct black/off color.
  constexpr RgbColor() = default;

  /**
   * @brief Construct an RGB color from channel intensities.
   * @param red Red channel intensity (0..255).
   * @param green Green channel intensity (0..255).
   * @param blue Blue channel intensity (0..255).
   */
  constexpr RgbColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}

  /// @brief Compare RGB channel equality.
  /// @param other Color to compare.
  /// @return true when all three channels match.
  constexpr bool operator==(const RgbColor& other) const {
    return r == other.r && g == other.g && b == other.b;
  }

  /// @brief Compare RGB channel inequality.
  /// @param other Color to compare.
  /// @return true when any channel differs.
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
  Alternate,
  SOS
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
  HazardAmber,
  Success,
  Connecting,
  LowBattery
};

/**
 * @brief Optional mode parameters.
 *
 * Not every mode reads every field: the fixed-pattern modes (DoubleBlink,
 * TripleBlink, Heartbeat, Alternate, SOS) and the random modes
 * (FlickerCandle, Glitch) ignore all of them. Each field documents its users.
 * Obtain sensible starting values from StatusLed::getModeDefaults().
 *
 * @note Values are sanitized on use: periodMs is raised to at least 2 ms,
 *       onMs is clamped to periodMs, and minLevel/maxLevel are swapped when
 *       inverted.
 */
struct ModeParams {
  /// @brief Full cycle length in ms.
  /// @note Used by BlinkSlow, BlinkFast, Strobe, Beacon, PulseSoft,
  ///       PulseSharp, Breathing and Throb. Valid range: 2..65535.
  uint16_t periodMs = 1000;

  /// @brief On-time within periodMs, in ms.
  /// @note Used by BlinkSlow, BlinkFast, Strobe and Beacon. 0 keeps the LED
  ///       off and onMs >= periodMs keeps it on; neither retransmits.
  uint16_t onMs = 500;

  /// @brief Ramp-up time for FadeIn, in ms.
  uint16_t riseMs = 800;

  /// @brief Ramp-down time for FadeOut, in ms.
  uint16_t fallMs = 800;

  /// @brief Lowest intensity reached (0..255).
  /// @note Used by FadeIn, FadeOut, PulseSoft, PulseSharp, Breathing, Throb.
  uint8_t minLevel = 0;

  /// @brief Highest intensity reached (0..255).
  /// @note Used by FadeIn, FadeOut, PulseSoft, PulseSharp, Breathing, Throb.
  uint8_t maxLevel = 255;
};

/**
 * @brief Snapshot of a single LED runtime state.
 */
struct LedSnapshot {
  /// @brief Current temporal mode.
  Mode mode = Mode::Off;
  /// @brief Current semantic preset, or Off when mode/color were set directly.
  StatusPreset preset = StatusPreset::Off;
  /// @brief Resting preset recorded by setDefaultPreset().
  StatusPreset defaultPreset = StatusPreset::Off;
  /// @brief Primary RGB color.
  RgbColor color{};
  /// @brief Secondary RGB color for alternate/composite modes.
  RgbColor altColor{};
  /// @brief Per-LED brightness (0..255).
  uint8_t brightness = 255;
  /// @brief Mode intensity computed by the most recent tick() (0..255).
  /// @note Setters do not recompute it; it updates on the next tick().
  uint8_t intensity = 0;
  /// @brief True while a temporary preset is active.
  bool tempActive = false;
  /// @brief True while a temporary preset is queued for the next tick().
  bool tempPending = false;
  /// @brief Milliseconds remaining for the active temporary preset.
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
  /// @brief Maximum number of LEDs supported by this build (default 10).
  /// @note Set STATUSLED_MAX_LED_COUNT to 1..255 as a build-wide compiler flag.
  ///       It changes class layout and fixed buffer sizes; every translation
  ///       unit, including the library, must use the same value.
  static constexpr uint8_t kMaxLedCount = STATUSLED_MAX_LED_COUNT;

  /// @brief Default constructor.
  StatusLed() = default;

  /// @brief Destructor releases backend resources.
  ~StatusLed() { end(); }

  /// @brief Non-copyable (owns backend pointer).
  StatusLed(const StatusLed&) = delete;
  StatusLed& operator=(const StatusLed&) = delete;

  /**
   * @brief Initialize the library with the given configuration.
   *
   * Must be called before tick(). Can be called again after end() to
   * reinitialize with different settings.
   *
   * @param config Configuration struct with pins and parameters.
   * @return Status Ok, INVALID_CONFIG when a field is out of range,
   *         OUT_OF_MEMORY when the backend cannot be allocated, or the
   *         backend's own error (typically HARDWARE_FAULT, with the driver
   *         code in Status::detail).
   *
   * @note Validates first, then calls end(), clears output-error history and
   *       allocates backend resources. Invalid engine configuration preserves
   *       the running instance and its history. The configuration is replaced
   *       only when initialization succeeds.
   */
  Status begin(const Config& config);

  /**
   * @brief Stop the library and release resources.
   *
   * Blanks the LEDs (best effort) before releasing the driver so they do not
   * stay lit. Safe to call multiple times. After end(), isInitialized()
   * returns false. Call begin() to restart.
   */
  void end();

  /**
   * @brief Cooperative update function. Call from loop().
   *
   * Performs bounded, non-blocking updates and transmits a frame only when a
   * pixel value changed. Wraparound of now_ms is handled.
   *
   * @param now_ms Current time in milliseconds (typically from millis()).
   *       Calls must be less than 0x80000000 ms apart for deadline comparisons.
   * @note Non-busy transmit failures update getLastStatus(), outputErrorCount()
   *       and lastOutputStatus(), and defer output polling/retry for 100 ms.
   *       Animations continue and pending frames coalesce during that interval.
   *       RESOURCE_BUSY is retried on the next call and is not an output error.
   */
  void tick(uint32_t now_ms);

  /**
   * @brief Set mode for a given LED using default parameters.
   * @param index LED index (0..ledCount-1).
   * @param mode Desired mode.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index or unknown mode.
   * @note Clears the LED's preset (it becomes custom state) and cancels any
   *       pending or active temporary preset on that LED.
   */
  Status setMode(uint8_t index, Mode mode);

  /**
   * @brief Set mode for a given LED with custom parameters.
   * @param index LED index (0..ledCount-1).
   * @param mode Desired mode.
   * @param params Custom parameters; see ModeParams for which modes use what.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index or unknown mode.
   * @note Clears the LED's preset and cancels any temporary preset on it.
   */
  Status setMode(uint8_t index, Mode mode, const ModeParams& params);

  /**
   * @brief Set primary color for a given LED.
   * @param index LED index (0..ledCount-1).
   * @param color RGB color.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index.
   * @note Clears the LED's preset and cancels any temporary preset on it.
   */
  Status setColor(uint8_t index, const RgbColor& color);

  /**
   * @brief Set secondary (alternate) color, used by Mode::Alternate.
   * @param index LED index (0..ledCount-1).
   * @param color RGB color.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index.
   * @note Clears the LED's preset and cancels any temporary preset on it.
   */
  Status setSecondaryColor(uint8_t index, const RgbColor& color);

  /**
   * @brief Set semantic preset (mode + colors) for a given LED.
   * @param index LED index (0..ledCount-1).
   * @param preset Preset definition.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index or unknown preset.
   * @note Overwrites both colors and cancels any temporary preset on that LED.
   */
  Status setPreset(uint8_t index, StatusPreset preset);

  /**
   * @brief Record the LED's resting preset.
   * @param index LED index (0..ledCount-1).
   * @param preset Default preset definition.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index or unknown preset.
   * @note Applied immediately only when the LED is idle (Mode::Off, no preset
   *       and no temporary preset). Otherwise it is only stored and reported
   *       in LedSnapshot::defaultPreset: it never overrides a running mode and
   *       is not restored when a temporary preset expires.
   */
  Status setDefaultPreset(uint8_t index, StatusPreset preset);

  /**
   * @brief Show a preset for a limited time, then revert.
   * @param index LED index (0..ledCount-1).
   * @param preset Temporary preset.
   * @param durationMs Duration in milliseconds (1..0x7FFFFFFF).
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index, unknown preset, or out-of-range duration.
   * @note Activates on the next tick(), which starts the duration and
   *       snapshots the state underneath (mode, parameters, colors, preset and
   *       the running animation's progress).
   * @note Calling it again while one is active replaces the overlay and
   *       restarts the timer, keeping the original snapshot underneath.
   * @note The interrupted animation pauses during the overlay: its elapsed
   *       progress, pattern phase and remaining step time resume on expiry.
   * @note Cancelled by setMode(), setColor(), setSecondaryColor(),
   *       setPreset(), the setAll*() calls, clearTemporary() and clear().
   *       setBrightness() is independent and survives the revert.
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
   * @brief Turn all LEDs off and reset state.
   *
   * Resets mode, presets, temporary overlays, colors, intensity and per-LED
   * brightness for every configured LED. The global brightness from Config is
   * kept. LEDs remain initialized; call end() to release resources.
   *
   * @return Status Ok on success, or NOT_INITIALIZED if begin() not called.
   */
  Status clear();

  /**
   * @brief Cancel a temporary preset and revert to the state underneath.
   * @param index LED index (0..ledCount-1).
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on a
   *         bad index.
   * @note Cancels a pending and an active temporary preset in one call. Safe
   *       when none is set (returns Ok).
   */
  Status clearTemporary(uint8_t index);

  /**
   * @brief Apply a preset to all configured LEDs.
   * @param preset Preset definition.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on an
   *         unknown preset.
   * @note Cancels temporary presets on all configured LEDs.
   */
  Status setAllPreset(StatusPreset preset);

  /**
   * @brief Apply a mode to all configured LEDs using default parameters.
   * @param mode Desired mode.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on an
   *         unknown mode.
   * @note Clears presets and cancels temporary presets on all configured LEDs.
   */
  Status setAllMode(Mode mode);

  /**
   * @brief Apply a mode to all configured LEDs with custom parameters.
   * @param mode Desired mode.
   * @param params Custom parameters; see ModeParams.
   * @return Status Ok, NOT_INITIALIZED before begin(), or INVALID_CONFIG on an
   *         unknown mode.
   * @note Clears presets and cancels temporary presets on all configured LEDs.
   */
  Status setAllMode(Mode mode, const ModeParams& params);

  /**
   * @brief Apply a primary color to all configured LEDs.
   * @param color RGB color.
   * @return Status Ok on success, or NOT_INITIALIZED if begin() not called.
   * @note Clears presets and cancels temporary presets on all configured LEDs.
   */
  Status setAllColor(const RgbColor& color);

  /**
   * @brief Request output retransmission on the next eligible tick().
   * @note Useful after suspected data line noise or external interference.
   *       Backend readiness and the 100 ms failure retry interval still apply.
   */
  void forceRefresh();

  /**
   * @brief Get a snapshot of LED state.
   * @param index LED index (0..ledCount-1).
   * @param out Snapshot output.
   * @return Status Ok on success, or INVALID_CONFIG on bad index.
   */
  Status getLedSnapshot(uint8_t index, LedSnapshot* out) const;

  /**
   * @brief Get the parameters that setMode(index, mode) would use.
   * @param mode Mode to query.
   * @return ModeParams defaults for that mode. Fixed-pattern and random modes
   *         return the struct defaults, which they then ignore.
   */
  static ModeParams getModeDefaults(Mode mode);

  /// @brief Check if library is currently initialized.
  /// @return true after successful begin() and before end().
  bool isInitialized() const { return _initialized; }

  /// @brief Get current configuration.
  /// @return Configuration accepted by the most recent successful begin().
  const Config& getConfig() const { return _config; }

  /// @brief Alias for getConfig(), matching shorter sibling-library accessors.
  /// @return Configuration accepted by the most recent successful begin().
  const Config& config() const { return getConfig(); }

  /// @brief Get the status recorded by the last fallible public operation.
  /// @note Also updated by tick() on non-busy transmit failures. Subsequent
  ///       setters overwrite it, including successful ones; use outputErrorCount()
  ///       and lastOutputStatus() for persistent output health. Const getters
  ///       do not change it.
  /// @return Last recorded status.
  Status getLastStatus() const { return _lastStatus; }

  /// @brief Alias for getLastStatus().
  /// @return Last status from a fallible public operation.
  Status lastStatus() const { return getLastStatus(); }

  /// @brief Count backend-rejected frames, excluding RESOURCE_BUSY.
  /// @return Failure count, saturating at UINT32_MAX instead of wrapping.
  /// @note Cleared when begin() passes engine validation and starts initializing;
  ///       retained across successful calls, successful output and end().
  uint32_t outputErrorCount() const { return _outputErrors; }

  /// @brief Get the most recent non-busy transmission failure.
  /// @return Last failed output status, or Ok if none has been recorded.
  /// @note Cleared alongside outputErrorCount() by begin(); successful output
  ///       and public calls do not erase it. The raw driver error is in detail.
  Status lastOutputStatus() const { return _lastOutputStatus; }

  /// @brief Get number of LEDs configured.
  /// @return Configured LED count, or the last accepted count after end().
  uint8_t ledCount() const { return _config.ledCount; }

 private:
#if STATUSLED_BACKEND_NULL && defined(STATUSLED_TEST)
  // Host-only access to seed boundary states without adding a production API.
  friend struct NullBackendTest::EngineAccess;
#endif

  /// @brief Per-LED runtime state (animation + temporary-preset overlay).
  struct LedState {
    // Current appearance.
    Mode mode = Mode::Off;
    ModeParams params{};
    RgbColor color{};
    RgbColor altColor{};
    uint8_t brightness = 255;
    uint8_t intensity = 0;
    bool useAlt = false;
    StatusPreset currentPreset = StatusPreset::Off;
    StatusPreset defaultPreset = StatusPreset::Off;

    // Animation scheduling. nextUpdateMs is a deadline; updateScheduled is
    // false once a mode has settled on a static value and needs no more work.
    uint32_t modeStartMs = 0;
    uint32_t nextUpdateMs = 0;
    bool updateScheduled = true;
    uint8_t phase = 0;
    uint32_t lfsr = 0xACE1u;

    // Temporary preset overlay.
    bool tempActive = false;
    bool tempPending = false;
    StatusPreset tempPreset = StatusPreset::Off;
    uint32_t tempUntilMs = 0;
    uint32_t tempDurationMs = 0;

    // State saved below an active temporary preset and restored on expiry.
    // resumeElapsedMs keeps the interrupted animation's progress so one-shot
    // modes (FadeIn/FadeOut) are not replayed from the start.
    Mode resumeMode = Mode::Off;
    ModeParams resumeParams{};
    RgbColor resumeColor{};
    RgbColor resumeAltColor{};
    StatusPreset resumePreset = StatusPreset::Off;
    uint32_t resumeElapsedMs = 0;
    uint32_t resumeStepRemainingMs = 0;
    uint8_t resumeIntensity = 0;
    uint8_t resumePhase = 0;
    bool resumeUseAlt = false;
    bool resumeUpdateScheduled = false;
  };

  Status setModeInternal(uint8_t index, Mode mode, const ModeParams& params);
  Status setColorInternal(uint8_t index, const RgbColor& color, bool secondary);
  Status applyPresetInternal(uint8_t index, StatusPreset preset);
  static void cancelTemporary(LedState& led);
  static void startMode(LedState& led, uint32_t startMs);
  void restoreFromTemporary(uint8_t index, uint32_t now_ms);
  void updateLed(uint8_t index, uint32_t now_ms);
  void refreshLedOutput(uint8_t index, uint8_t intensity, bool useAlt);
  void refreshLedOutput(uint8_t index);

  bool indexValid(uint8_t index) const { return index < _config.ledCount && index < kMaxLedCount; }
  Status setLast(const Status& st) {
    _lastStatus = st;
    return st;
  }

  Config _config{};
  bool _initialized = false;
  Status _lastStatus{};
  Status _lastOutputStatus{};
  uint32_t _outputErrors = 0;
  uint32_t _outputRetryAtMs = 0;
  bool _outputRetryPending = false;
  uint32_t _lastTickMs = 0;
  bool _timeSynced = false;
  bool _frameDirty = false;

  LedState _leds[kMaxLedCount]{};
  RgbColor _frame[kMaxLedCount]{};
  BackendBase* _backend = nullptr;
};

}  // namespace StatusLed
