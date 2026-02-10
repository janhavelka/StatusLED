# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Nothing yet

### Changed
- Nothing yet

### Deprecated
- Nothing yet

### Removed
- Nothing yet

### Fixed
- Nothing yet

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

[Unreleased]: https://github.com/janhavelka/StatusLED/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.1
[1.0.0]: https://github.com/janhavelka/StatusLED/releases/tag/v1.0.0
