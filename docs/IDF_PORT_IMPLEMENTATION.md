# StatusLED ESP-IDF Port Implementation Notes

Date: 2026-05-19.
Branch: `feature/statusled-idf-port`.

## Scope

- Kept the public API and animation engine framework-neutral.
- Added a pure ESP-IDF component build using only the IDF5 RMT v2 backend.
- Preserved Arduino/PlatformIO CLI examples and existing backend macro names.
- Added a native ESP-IDF example under `examples/espidf_basic` that shares the
  same colored interactive CLI source as the Arduino example.

## Files Added

- `CMakeLists.txt`
- `idf_component.yml`
- `examples/espidf_basic/CMakeLists.txt`
- `examples/espidf_basic/main/CMakeLists.txt`
- `examples/espidf_basic/main/main.cpp`
- `examples/common/IdfArduinoCompat.h`
- `scripts/check_idf_example_contract.py`

## Audit Resolution

- Legacy RMT exclusion:
  - The pure IDF component lists only `src/StatusLed.cpp` and
    `src/StatusLedBackendIdf5.cpp`.
  - CMake defines exactly one backend macro:
    `STATUSLED_BACKEND_IDF5_WS2812=1`.
- RMT payload lifetime:
  - `StatusLedBackendIdf5` now stores transmit bytes in a backend-owned
    `_payload` buffer before calling `rmt_transmit()`.
  - The cleanup blank frame also reuses backend-owned payload storage.
- Callback synchronization:
  - `_txBusy` is now `std::atomic<bool>`.
  - The transmit-done callback only clears the flag; normal code uses acquire
    loads before submitting a new frame.
- Native IDF example:
  - `app_main()` defines `STATUSLED_EXAMPLE_PLATFORM_IDF`, includes the
    example-local Arduino compatibility layer, and includes
    `examples/01_status_led_cli/main.cpp`.
  - ESP-IDF now exposes the same help structure, ANSI coloring, lifecycle
    commands, status/config views, mode/preset lists, stress controls, and
    LED-control commands as the Arduino CLI.
  - `scripts/check_idf_example_contract.py` statically guards the shared-source
    include, required CMake dependencies, compatibility shim, and CLI command
    surface.

## Remaining Hardware Checks

- Build `examples/espidf_basic` with ESP-IDF v6.0.1 for `esp32s3` and `esp32s2`;
  `idf.py` was not available on PATH in this shell.
- Run WS2812 hardware smoke, `RESOURCE_BUSY` stress, and `end()` cleanup tests.

## Verification

- `python -m platformio test -e native`: passed.
- `python -m platformio run -e cli_esp32s3_idf5`: passed.
- `python -m platformio run -e cli_esp32s2_idf5`: passed.
- `python -m platformio run -e cli_esp32s3_idf`: passed.
- `python -m platformio run -e cli_esp32s2_idf`: passed.
- `python scripts/generate_version.py`: passed; generated `Version.h` was not committed.
- `python scripts/check_text_integrity.py`: passed.
- `git diff --check`: passed.
