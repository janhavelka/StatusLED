# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- StatusLed library with non-blocking status LED engine
- Mode/preset architecture with color separation and dirty-frame updates
- NeoPixelBus backend (RMT) with configurable channel selection
- Fallback backend option for WS2812 driver
- Interactive CLI example with full API access and stress test
- Host-based unit tests for timing/state transitions

### Changed
- Updated README and AGENTS guidelines for status LED subsystem
- Updated platformio.ini environments and dependencies

### Deprecated
- Nothing yet

### Removed
- Nothing yet

### Fixed
- Nothing yet

### Security
- Nothing yet

## [0.1.0] - 2026-01-10

### Added
- Initial release with template structure
- ESP32-S2 and ESP32-S3 support

[Unreleased]: https://github.com/YOUR_USERNAME/status-led/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/YOUR_USERNAME/status-led/releases/tag/v0.1.0
