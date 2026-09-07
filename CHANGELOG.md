# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.5.0] - 2026-09-07

### Added

- Persistent `outputErrorCount()` and `lastOutputStatus()` diagnostics. The
  counter saturates, excludes `RESOURCE_BUSY`, and survives successful calls
  and output until a new validated initialization attempt.
- Build-wide `STATUSLED_MAX_LED_COUNT` override (1..255, default 10), with
  compile-time bounds checks and a native test configuration at maximum capacity.
- Optional `Config::rmtFullFrameBuffer` for both RMT backends: reserves data,
  reset and stop-marker memory before output to avoid flash-write refill glitches.
  Invalid sizes/channels and unavailable resources return errors.
- CI builds Arduino RMT v2 on S2/S3 and the native ESP-IDF component on 5.3/6.0,
  including cache-safe S3 configurations; feature branches run the same checks.

### Fixed

- Native capacity tests now independently require 10/255 LEDs, detecting a
  missing or incorrect maximum-capacity compiler flag. Existing tests additionally
  cover counter saturation, rejected temporary-preset ordering and output-history
  clearing after a backend initialization failure using host-only controls.
- Cache-safe ESP-IDF CI appends to existing SDK defaults and verifies the exact
  option in the generated `sdkconfig`, rejecting unknown or ineffective settings.
- Shared CLI parsing guards its wide-host overflow check by `ULONG_MAX`, retaining
  strict 32-bit bounds without a redundant comparison on 32-bit `unsigned long`.
- Restore ESP-IDF 6.0 builds on S2/S3 after the SDK moved the RMT channel-count
  capability into its HAL. Full-frame buffer validation keeps the total memory
  capacity, including borrowable RX blocks on S3, across IDF 5.x and 6.x.
- Non-busy output failures defer polling/retry for 100 ms using wraparound-safe
  deadlines. Animation updates continue and coalesce while output recovers.
- Both RMT backends allocate once in internal RAM, preserving multiple instances
  without PSRAM fallback. IDF5 encoder and completion callbacks live in IRAM for
  cache-safe SDK builds, and transmit submissions explicitly avoid queue waits.
- RMT zero bits now use 325 ns high and 925 ns low at 40 MHz, preserving a
  1.25 us bit period and meeting the newer zero-high window. One-bit timings
  remain unchanged; compatibility still requires qualification of the LED revision.
- Legacy failed-initialization cleanup releases the GPIO even when driver
  installation fails. IDF5 shutdown explicitly detaches the GPIO matrix on
  ESP-IDF 6.x as well as 5.x. Shutdown submits the blank frame asynchronously and waits
  within a fixed budget. Legacy resource checks reject overlapping active blocks.
- Pulse modes normalize their clock origin each cycle, retaining phase after
  more than 49.7 days of continuous operation.
- Temporary overlays preserve blink/pattern phase and remaining step time, as
  well as fade/pulse progress; completed fades remain complete across clock wrap.
  Rejected preset values no longer cancel an active temporary preset.
- Both CLIs share strict unsigned decimal parsing, including GPIO arguments;
  signed/overflowing/trailing input and invalid color-order names are rejected.
  Both expose full-frame buffering in `begin` and persistent output health in `info`.
  CLI initialization/configuration mirrors the actual library state after a
  rejected reinitialization, preserving a still-running instance.
- NeoPixelBus 2.7.6 dependencies use the exact upstream Git commit because the
  previous registry specification no longer resolves on a clean installation.

### Documentation

- Scoped the historical audit's findings and baseline verification to `a7e0e4e`,
  with the implementation review providing the current state.
- Aligned initialization-history wording, audit links and both native test
  requirements; documented the IDF5 busy-error mapping's enabled-channel assumption.
- Hardware smoke tests are optional in the engineering guidelines and no longer
  block commits or pushes; validation reports still state whether they were run.
- Reverified every original audit finding, corrected its PSRAM, timing arithmetic,
  package-switching and resolved-cleanup claims, and recorded the decisions and
  validation limits in `docs/CODE_AUDIT_REVIEW.md`.
- Documented RMT memory costs, cache-safe SDK settings, output retry/health
  semantics, capacity ABI requirements and remaining timing limitations.

## [1.4.0] - 2026-09-04

Correctness release from a full audit of the engine, both RMT backends and the
documentation. Remaining operational limitations are documented in README.

### Fixed

- **WS2812 latch gap.** The IDF5 RMT backend emitted no reset symbol at all, so
  two frames sent close together were concatenated by the LEDs: pixel data
  shifted down the chain instead of latching. It now uses a composite encoder
  (bytes + copy) that appends a 300 us reset, matching the ESP-IDF `led_strip`
  encoder. The legacy RMT backend's gap was 80 us, which meets the original
  WS2812B datasheet but not the >280 us required by WS2812B-V5 / WS2812B-2020 /
  WS2812C parts; it is now 300 us as well.
- **RMT channel over-allocation on ESP32-S3.** The IDF5 backend requested
  `mem_block_symbols = 64`, but an S3 channel owns 48 words, so the driver
  rounded up to two blocks and silently consumed a second TX channel. It now
  uses `SOC_RMT_MEM_WORDS_PER_CHANNEL`.
- **Data line left floating after `end()`.** `rmt_del_channel()` calls
  `gpio_reset_pin()`, which leaves the pad an input with a pull-up, and the
  legacy driver's `rmt_driver_uninstall()` leaves the pad routed to a dead RMT
  signal. Both backends now detach the signal and hold the data line low.
- **Pattern corruption once every 256 steps.** The per-LED phase counter was a
  `uint8_t` incremented without bound and reduced modulo the table length. For
  tables whose length does not divide 256 the sequence jumped at the wrap, so
  SOS restarted mid-message roughly every 48 s and TripleBlink dropped its rest
  gap every 45 s.
- **`clearTemporary()` ignored an active temporary preset** when a second one
  was queued behind it, leaving the LED stuck on the overlay.
- **Temporary presets replayed finished one-shot fades.** Reverting restored the
  mode but not its progress, so a completed `FadeOut` jumped back to full
  brightness and faded out again after every temporary blip.
- **Degenerate blink duty cycles blipped and retransmitted.** `onMs == periodMs`
  or `onMs == 0` produced a zero-length phase every period, emitting two frames
  per period for what should be a static LED.
- **`minLevel`/`maxLevel` were not the output bounds** for the eased modes:
  shaping was applied after interpolation, so `Breathing` swung the full 0..255
  regardless of its documented `minLevel = 20` floor.
- **Pulse modes were phase-locked to absolute time**, so a pulse started
  mid-cycle and every LED with the same period moved in lockstep. They now run
  from the moment the mode was set.
- **Repeating modes drifted** by one tick interval per step; deadlines now
  advance from the previous deadline, with a resync after a long stall.
- **`begin()` adopted a rejected configuration.** A backend failure left
  `config()` and `ledCount()` reporting settings that were never accepted.
- **`clear()` left stale state**: per-LED brightness and the last computed
  intensity survived it, and `getLedSnapshot()` reported them until the next
  tick.
- `getModeDefaults(SOS)` advertised a 4200 ms cycle; the pattern is 3400 ms.
- The Arduino CLI accepted trailing garbage in numeric arguments (`12abc`) and
  truncated an out-of-range LED index (`status 256` acted on LED 0).
- The legacy backend left the data pin routed to the RMT peripheral when
  `rmt_config()` succeeded but `rmt_driver_install()` failed, because the pin was
  only recorded after installation. `end()` now releases it on that path too.
- The NeoPixelBus backend failed with a confusing error deep inside the
  dependency when selected on Arduino core 3.x. It now rejects that combination
  itself with an `#error`, placed inside its own backend block so no other build
  is affected. NeoPixelBus reaches the LEDs through the legacy RMT driver, which
  Arduino core 3.x does not provide and ESP-IDF 6.0 removed entirely.

### Changed

- **Temporary presets now have one consistent cancellation rule.** `setMode()`,
  `setColor()`, `setSecondaryColor()`, `setAllMode()` and `setAllColor()` cancel
  a temporary preset the way `setPreset()` always did, instead of being silently
  reverted when it expired. Per-LED brightness is no longer part of the
  saved state, so `setBrightness()` during an overlay is no longer undone.
- **`setDefaultPreset()` no longer overrides a running temporary preset.** It
  applies immediately only when the LED is genuinely idle, and is otherwise
  stored and reported through `LedSnapshot::defaultPreset`.
- `Strobe` and `Beacon` are duty-cycle modes driven by `ModeParams`, sharing the
  blink implementation instead of fixed step tables. Their default timings are
  unchanged (100/50 ms and 4000/80 ms).
- `FadeIn`/`FadeOut` interpolate between `minLevel` and `maxLevel` rather than
  a hardcoded 0..255.
- The seven near-identical pattern blocks in `updateLed()` collapsed into one
  table-driven step, so a scheduling fix cannot be applied to some modes only.
- The two RMT backends share `writePixelBytes()` for wire byte order, replacing
  a `mapColorOrder()` helper whose argument meaning differed between them. The
  NeoPixelBus backend still converts through its own pixel type, since it never
  handles raw wire bytes.
- ESP-IDF component requirement relaxed from `>=6.0.1` to `>=5.3`, the first
  release with the `esp_driver_rmt` component. The RMT TX API used here is
  unchanged through 6.x.
- `LedSnapshot` gained `tempPending`, so a queued temporary preset is visible
  before it activates.
- Public Doxygen now states, per method, which state it cancels and which
  `ModeParams` fields each mode reads. `ColorOrder` now states its scope: 24-bit
  WS2812-class parts only, with no support for RGBW or BRG variants.
- Reverting a temporary preset, whether by expiry or `clearTemporary()`, now
  updates the LEDs on the next `tick()` instead of writing the frame
  synchronously. The visible result is the same, one tick later.
- The falling half of a pulse cycle is now scaled by its own length rather than
  by the rising half's. Only odd `periodMs` values were affected, where the ramp
  previously ran slightly short.
- `setTemporaryPreset()` reports an out-of-range duration as one status,
  `"durationMs out of range"`, instead of two separate messages, and puts the
  offending value in `Status::detail`. The NeoPixelBus backend likewise reports
  a bad pixel count as a single `"count out of range"`. Callers that matched on
  the old message strings need updating; the `Err` codes are unchanged.
- The ESP-IDF example CLI renamed four keywords to match the Arduino CLI:
  `flickercandle` to `flicker`, `alarmpolice` to `police`, `hazardamber` to
  `hazard`, and `lowbattery` to `lowbat`. The old words no longer parse.
- The RMT v2 backend waits up to 50 ms rather than 10 ms for the queue to drain
  in `end()`, so a slow transfer completes instead of being abandoned.

### Added

- 15 regression tests, 43 host tests in total. They cover the engine fixes above.
  The backend, CLI and configuration-rejection fixes are not reachable from the
  host build and were verified by compilation and review instead.

### Removed

- `docs/IDF_PORT.md` and `docs/IDF_PORT_IMPLEMENTATION.md`: port work logs whose
  durable content is in README, and whose "current state" sections had gone
  stale.
- `scripts/check_idf_example_contract.py`: a static text-matching guard on the
  ESP-IDF example that was not run by CI and duplicated review.
- An unused `frameDurationUs()` helper and its bit-period constant, added during
  the audit and never called.

## [1.3.0] - 2026-03-01

### Changed
- Refined CLI example/log presentation for consistency with the current unified help/reporting scheme.

### Fixed
- Improved output readability by limiting emphasis to operationally important statuses.
- IDF5 WS2812 backend no longer polls `rmt_tx_wait_all_done(..., 0)` in normal transmit flow, preventing repeated `flush timeout` log spam under high-frequency updates (e.g., CLI stress mode).

## [1.2.0] - 2026-02-24

### Added
- ESP-IDF 5.x RMT v2 backend behind `STATUSLED_BACKEND_IDF5_WS2812` (compile-time selectable).
- PlatformIO IDF5 build environments for ESP32-S2/S3 CLI example targets.

## [1.1.0] - 2026-02-22

### Added
- `clear()` method to turn all LEDs off and reset state in a single call.
- `clearTemporary(index)` method to cancel a temporary preset early and revert.
- `setAllPreset(preset)` method to apply a preset to all configured LEDs.
- `setAllMode(mode)` / `setAllMode(mode, params)` methods to apply mode to all LEDs.
- `setAllColor(color)` method to apply color to all configured LEDs.
- `forceRefresh()` method to force output retransmission on next tick.
- `Mode::SOS` — Morse code SOS distress pattern (...---...).
- `StatusPreset::Success` — DoubleBlink Green for operation-complete indication.
- `StatusPreset::Connecting` — PulseSoft Blue for IoT/WiFi connecting states.
- `StatusPreset::LowBattery` — Beacon Red for sparse low-battery indication.
- Deleted copy/move constructors and assignment operators to prevent double-free.
- 21 new unit tests covering guards, edge cases, new modes, presets, and methods.
- CLI commands: `clear`, `cleartemp`, `allpreset`, `allmode`, `allcolor`, `refresh`.

### Fixed
- LFSR zero-lockup: added guard to prevent FlickerCandle/Glitch modes from freezing if LFSR state reaches zero.
- LFSR seed now properly initialized within 16-bit polynomial range (was 20-bit, causing unpredictable initial sequence).
- Added bounds check in `refreshLedOutput(index)` single-argument overload to prevent out-of-bounds array access.
- IDF backend now blanks all LEDs (sends zeros) before driver uninstall in `end()`, preventing LEDs stuck in last state.

## [1.0.2] - 2026-02-11

### Added
- Added `scripts/check_text_integrity.py` to fail fast on UTF-8 BOM in tracked text files.
- Added CI enforcement for text-integrity checks in `.github/workflows/ci.yml`.

### Changed
- Nothing yet

### Deprecated
- Nothing yet

### Removed
- Nothing yet

### Fixed
- Removed UTF-8 BOM from `library.json` and tracked source/header files to restore reliable PlatformIO manifest parsing from `lib_deps`.

### Security
- Nothing yet

## [1.0.1] - 2026-02-10

### Added
- Nothing yet

### Changed
- Hardened scheduler internals to avoid `millis()` wraparound freeze edge cases.

### Deprecated
- Nothing yet

### Removed
- Nothing yet

### Fixed
- Fixed descending interpolation in fade paths (e.g. `FadeOut`) to prevent intensity corruption.
- Fixed a deadline sentinel collision that could freeze repeating modes near `uint32_t` timer wrap.
- Added defensive config validation for invalid `colorOrder` and out-of-range `dataPin`.
- Added backend argument/error guards for null frame pointers, invalid counts, and RMT wait faults.
- Added regression tests for wraparound scheduling, fade-out behavior, and config validation.
- Fixed PlatformIO env definitions so S2/S3 example targets always have explicit `board` configuration.

### Security
- Nothing yet

## [1.0.0] - 2026-02-02

### Added
- StatusLed library with non-blocking status LED engine
- Mode/preset architecture with color separation and dirty-frame updates
- NeoPixelBus backend (RMT) with configurable channel selection
- IDF WS2812 backend (legacy RMT) with safe-by-default selection
- Interactive CLI example with full API access and stress test
- Host-based unit tests for timing/state transitions

### Changed
- Updated README and AGENTS guidelines for status LED subsystem
- Updated PlatformIO environments and backend selection macros

### Removed
- Removed compile-only example in favor of a single fully featured CLI demo

### Fixed
- Hardened backend selection guards and compilation isolation
- Added bounds checks and nonblocking guards in backends and engine

### Security
- Nothing yet

[1.3.0]: https://github.com/janhavelka/StatusLED/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.2.0
[1.1.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.1.0
[1.0.2]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.2
[1.0.1]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.1
[1.0.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.0

[1.4.0]: https://github.com/janhavelka/StatusLED/compare/v1.3.0...v1.4.0
[1.5.0]: https://github.com/janhavelka/StatusLED/compare/v1.4.0...v1.5.0
[Unreleased]: https://github.com/janhavelka/StatusLED/compare/v1.5.0...HEAD
