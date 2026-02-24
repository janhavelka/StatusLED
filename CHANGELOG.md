# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[1.2.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.2.0
[1.1.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.1.0
[1.0.2]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.2
[1.0.1]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.1
[1.0.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.0
