# StatusLED ESP-IDF v6.0.1 Port

Date: 2026-05-17.
Updated: 2026-05-19.
Scope: keep the StatusLED animation core usable from both Arduino/PlatformIO
and pure ESP-IDF while preserving the existing CLI example behavior.

## Result

- The animation core remains framework-neutral. `StatusLed.cpp` has no Arduino
  or ESP-IDF timing ownership; applications drive it with `tick(nowMs)`.
- Pure ESP-IDF builds use the IDF5 RMT v2 backend only:
  `STATUSLED_BACKEND_IDF5_WS2812=1`.
- The IDF v6 component does not compile the legacy `<driver/rmt.h>` backend or
  the Arduino/NeoPixelBus backend.
- The IDF5 backend no longer passes a stack payload to queued RMT
  transmissions. Payload bytes are held in backend-owned storage until the
  transfer completes.
- The RMT done callback now clears an atomic busy flag instead of writing a
  `volatile bool`.
- Root `CMakeLists.txt` and `idf_component.yml` make the library consumable as
  an ESP-IDF component.
- `examples/espidf_basic` provides a native `app_main()` that shares the same
  colored interactive CLI source as the Arduino example while using the IDF5
  backend.

## Current State

- The animation engine is mostly framework-neutral:
  `StatusLed::begin(const Config&)`, `tick(uint32_t nowMs)`, `end()`, mode
  setters, fixed LED buffers, and backend abstraction.
- `include/StatusLed/BackendConfig.h` requires exactly one backend macro:
  `STATUSLED_BACKEND_IDF_WS2812`, `STATUSLED_BACKEND_IDF5_WS2812`,
  `STATUSLED_BACKEND_NEOPIXELBUS`, or `STATUSLED_BACKEND_NULL`.
- `src/StatusLedBackendIdf.cpp` uses the removed legacy RMT API
  `<driver/rmt.h>`.
- `src/StatusLedBackendIdf5.cpp` uses the newer RMT TX APIs:
  `<driver/rmt_tx.h>` and `<driver/rmt_encoder.h>`.
- `src/StatusLedBackendNeoPixelBus.cpp` is Arduino/NeoPixelBus oriented.
- `src/StatusLedBackendNull.cpp` is suitable for host/unit tests.
- `platformio.ini` selects different Arduino backend macros by environment.
- Pure ESP-IDF component metadata and `examples/espidf_basic` are present.
- PlatformIO environment names such as `cli_esp32s3_idf` are not pure ESP-IDF
  v6 validation. They still use `framework = arduino` and may select the legacy
  RMT backend; treat them as Arduino regression checks only.

## Previous Blockers Resolved

- IDF v6 removed the legacy RMT driver. The pure IDF component does not compile
  `src/StatusLedBackendIdf.cpp` or include `<driver/rmt.h>`.
- `STATUSLED_BACKEND_NEOPIXELBUS` is not a pure IDF backend and is excluded from
  the pure IDF component.
- `STATUSLED_BACKEND_IDF5_WS2812` now has explicit CMake dependencies,
  backend-owned TX payload storage, callback-safe busy-state handoff, and
  `esp_err_t` to `Status` error mapping at the backend boundary.
- Root `CMakeLists.txt`, `idf_component.yml`, and a native IDF example are
  present.

## Exact Files and APIs to Change

- `include/StatusLed/BackendConfig.h`
  - Keep the existing macro names for Arduino compatibility.
  - Add documentation or defaults for IDF v6 selecting
    `STATUSLED_BACKEND_IDF5_WS2812=1`.
  - Do not select the legacy backend for IDF v6.
- `src/StatusLed.cpp`
  - Keep the backend factory and public API stable.
  - Confirm `new` in `begin()` is acceptable for this library or replace with
    static backend storage if the no-heap rule must be strict.
- `src/StatusLedBackendIdf.cpp`
  - Exclude from IDF v6 builds. Do not include `<driver/rmt.h>`.
  - Keep only for old Arduino/IDF environments if still required.
- `src/StatusLedBackendIdf5.cpp`
  - Treat as the IDF v6 RMT backend candidate.
  - Audit `rmt_new_tx_channel()`, `rmt_new_bytes_encoder()`, `rmt_enable()`,
    `rmt_transmit()`, `rmt_tx_wait_all_done()`, and delete paths.
  - Make payload lifetime and callback synchronization deterministic.
- Optional future backend:
  - Add `src/StatusLedBackendLedStrip.cpp` only if the official `led_strip`
    component is preferred over the custom RMT encoder.

## Compatibility Architecture

- Keep the public `StatusLed` API unchanged.
- Keep compile-time backend selection. IDF v6 should compile exactly one of:
  - `STATUSLED_BACKEND_IDF5_WS2812=1` using new RMT TX APIs.
  - A new LED-strip backend using the official `led_strip` component.
  - `STATUSLED_BACKEND_NULL=1` for tests.
- Arduino builds may continue using existing Arduino-compatible backends.
- Do not nest IDF backend selection inside application code. Select the backend
  with CMake compile definitions.
- `tick(nowMs)` remains the only animation scheduler. IDF examples provide
  `nowMs` from `esp_timer_get_time() / 1000`.
- No repeated sends: only call backend `show()` when the rendered frame changes
  or a timed transition requires it.

## Adapter Contract

Backend contract:

- `begin(const Config&)` initializes hardware and allocates any required driver
  resources.
- `show(const Rgb* pixels, size_t count)` queues or transmits exactly one frame
  and returns `RESOURCE_BUSY` if a previous frame is still active.
- `canShow()` reports whether a new frame may be submitted.
- `end()` returns the LED output to a deterministic safe state and deletes IDF
  driver resources.
- All ESP-IDF errors map to `Status`; store raw `esp_err_t` in
  `Status::detail`.

IDF v6 RMT backend:

- Include `<driver/rmt_tx.h>` and `<driver/rmt_encoder.h>`.
- Link component `esp_driver_rmt` and GPIO dependency `esp_driver_gpio` if GPIO
  configuration is done directly.
- Keep RMT transaction payload storage valid until transmission is complete.
- Use `rmt_tx_register_event_callbacks()` only with callback-safe state updates.
- Use finite `rmt_tx_wait_all_done()` timeouts in `end()` and cleanup paths.

IDF LED-strip backend option:

- Depend on `espressif/led_strip` via `idf_component.yml`.
- Use it as the owner of RMT/LED-strip details.
- Do not compile the custom RMT backend at the same time for the same LED GPIO.

## CMake and Component Plan

RMT v2 component:

```cmake
idf_component_register(
  SRCS
    "src/StatusLed.cpp"
    "src/StatusLedBackendIdf5.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_rmt esp_driver_gpio esp_timer
)
target_compile_definitions(${COMPONENT_LIB}
  PUBLIC STATUSLED_BACKEND_IDF5_WS2812=1
)
```

Null-test component variant:

```cmake
target_compile_definitions(${COMPONENT_LIB}
  PUBLIC STATUSLED_BACKEND_NULL=1
)
```

Do not list `src/StatusLedBackendIdf.cpp` or
`src/StatusLedBackendNeoPixelBus.cpp` in pure IDF v6 builds.

Optional `led_strip` metadata:

```yaml
version: "1.3.0"
description: "Status LED animation engine"
targets:
  - esp32s2
  - esp32s3
dependencies:
  idf: ">=6.0.1"
  espressif/led_strip: "*"
```

If using the custom RMT backend only, omit the `led_strip` dependency.

## Example Plan

- IDF example:
  - `examples/espidf_basic/main/main.cpp` defines
    `STATUSLED_EXAMPLE_PLATFORM_IDF`, includes
    `examples/common/IdfArduinoCompat.h`, and then includes
    `examples/01_status_led_cli/main.cpp`.
  - The Arduino and ESP-IDF examples therefore expose the same help grouping,
    colorized output, lifecycle commands, status/config views, stress mode,
    mode/preset lists, and per-LED control commands.
  - `IdfArduinoCompat.h` provides only the small example-local `Serial`,
    `millis()`, `delay()`, `yield()`, and `F()` surface needed by the CLI.
  - The ESP-IDF `app_main()` calls the shared `setup()` / `loop()` flow and
    yields with `vTaskDelay()`.
- Arduino example:
  - Keep existing Arduino examples and backend macros unchanged.
  - Add an Arduino build check after CMake files are added.

## Test And Validation Plan

- Host/unit with `STATUSLED_BACKEND_NULL=1`:
  - State transitions, blink cadence, pulse/smooth transitions, priority rules,
    brightness scaling, and invalid config handling.
- IDF build:
  - Component build for `esp32s2` and `esp32s3`.
  - Example build with `STATUSLED_BACKEND_IDF5_WS2812=1`.
- Hardware:
  - WS2812 smoke test for configured `ledCount` and `ColorOrder`.
  - Repeated `setMode()`/`tick()` test confirms no repeated sends when pixels
    are unchanged.
  - Stress test for `RESOURCE_BUSY` path and `end()` cleanup.
  - Logic analyzer check for WS2812 timing if using the custom RMT backend.

## ESP-IDF v6.0.1 Hazards

- Legacy RMT `<driver/rmt.h>` is removed. Do not include or compile it.
- New RMT TX APIs require component `esp_driver_rmt`.
- Do not mix legacy RMT and new RMT headers in one build.
- If using `led_strip`, let that component own the RMT channel; do not also
  create a custom RMT TX channel on the same GPIO.
- RMT callbacks may run in ISR or driver task context depending on driver
  configuration. Keep callbacks tiny and avoid logging.
- RMT transmit payload lifetime must be explicit. Stack buffers are unsafe if
  the transaction can outlive `show()`.
- `Config::rmtChannel` is ignored by the current new-RMT backend. IDF v6 RMT
  allocates channels dynamically; document this or remove use in IDF builds.

## Validation

Completed locally:

- `python -m platformio test -e native`
- `python -m platformio run -e cli_esp32s3_idf5`
- `python -m platformio run -e cli_esp32s2_idf5`
- `python -m platformio run -e cli_esp32s3_idf`
- `python -m platformio run -e cli_esp32s2_idf`
- `python scripts/generate_version.py`
- `python scripts/check_text_integrity.py`
- `git diff --check`

Pending in this shell:

- `idf.py build` for the shared-source CLI in `examples/espidf_basic`
- Hardware smoke, busy-path, and cleanup tests

`idf.py` was not available on PATH during this implementation pass, so the
ESP-IDF example is implemented and documented but still needs a real ESP-IDF
toolchain build before release.
