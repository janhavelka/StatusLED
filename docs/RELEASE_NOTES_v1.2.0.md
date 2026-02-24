# StatusLED v1.2.0 Release Notes

Release date: 2026-02-24

## Highlights

- Added a new ESP-IDF 5.x WS2812 backend using RMT v2 (`STATUSLED_BACKEND_IDF5_WS2812`).
- Kept backward compatibility with existing IDF 4.4 backend (`STATUSLED_BACKEND_IDF_WS2812`).
- Added dedicated IDF5 PlatformIO environments for ESP32-S2 and ESP32-S3 CLI example builds.

## Added

- New backend implementation:
  - `src/StatusLedBackendIdf5.cpp`
  - Uses `driver/rmt_tx.h` and `driver/rmt_encoder.h`
  - Uses `rmt_new_bytes_encoder()` for deterministic WS2812 byte encoding
- Backend selection updates:
  - `include/StatusLed/BackendConfig.h`
  - New compile-time macro: `STATUSLED_BACKEND_IDF5_WS2812`
  - Mutual exclusion checks updated to enforce exactly one backend
- Build environments:
  - `cli_esp32s3_idf5`
  - `cli_esp32s2_idf5`
- Documentation updates:
  - README backend matrix and selection guidance
  - Config notes clarifying `rmtChannel` behavior under IDF5 backend

## Compatibility

- Public API unchanged (`StatusLed.h`, `Config.h`, `Status.h`).
- Existing IDF 4.4 and NeoPixelBus workflows remain available.
- Backend is selected at compile time (no runtime auto-detection).

## Operational Notes

- IDF5 backend ignores `Config::rmtChannel` because channel allocation is dynamic in RMT v2.
- `show()` remains non-blocking and returns `RESOURCE_BUSY` when transmit is in progress.
- No heap allocation is performed in steady-state transmit path.

## Validation Summary

- Native unit tests passed.
- IDF 4.4 CLI builds passed (`cli_esp32s3_idf`, `cli_esp32s2_idf`).
- IDF5 CLI builds passed (`cli_esp32s3_idf5`, `cli_esp32s2_idf5`).

## Breaking Changes

- None.

## Upgrade Guidance

- Keep IDF 4.4 builds on:
  - `STATUSLED_BACKEND_IDF_WS2812=1`
- Use IDF5 builds on:
  - `STATUSLED_BACKEND_IDF5_WS2812=1`
- Ensure exactly one `STATUSLED_BACKEND_*` macro is enabled per build.
