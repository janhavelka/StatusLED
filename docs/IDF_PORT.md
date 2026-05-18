# StatusLED ESP-IDF v6.0.1 Port Readiness Audit

Date: 2026-05-17.
Scope: documentation for a future ESP-IDF port only. Do not change code,
`library.json`, `README.md`, `CHANGELOG.md`, generated files, examples, or
tests while applying this audit.

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
- `platformio.ini` currently selects different backend macros by environment.
  There is no pure ESP-IDF component build or IDF example.
- PlatformIO environment names such as `cli_esp32s3_idf` are not pure ESP-IDF
  v6 validation. They still use `framework = arduino` and may select the legacy
  RMT backend; treat them as Arduino regression checks only.

## Blockers

- IDF v6 removed the legacy RMT driver. Any IDF v6 build that compiles
  `src/StatusLedBackendIdf.cpp` or includes `<driver/rmt.h>` will fail.
- `STATUSLED_BACKEND_NEOPIXELBUS` is not a pure IDF backend.
- `STATUSLED_BACKEND_IDF5_WS2812` is the only current candidate for IDF v6, but
  it needs an IDF v6 audit for CMake dependencies, TX buffer lifetime, callback
  synchronization, and error handling.
- `src/StatusLedBackendIdf5.cpp` builds a stack `payload` in `show()` and calls
  `rmt_transmit()`. This is a hard IDF v6 blocker: for queued/nonblocking RMT
  transactions, move the payload to backend-owned storage or wait for completion
  before returning.
- `_txBusy` is modified from the RMT done callback and read by normal code.
  Replace the `volatile bool` pattern with a callback-safe primitive or a
  documented critical-section/atomic handoff before treating the backend as
  production-ready.
- No root `CMakeLists.txt`, `idf_component.yml`, or IDF-native example exists.

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
- Optional new backend:
  - Add `src/StatusLedBackendLedStrip.cpp` if the official `led_strip`
    component is preferred over a custom RMT encoder.
- Build/example files to add later:
  - `CMakeLists.txt`
  - `idf_component.yml`
  - `examples/espidf_basic/`

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
  - `examples/espidf_basic/main/main.cpp` with `app_main()`.
  - Configure `StatusLed::Config` locally: `dataPin`, `ledCount`,
    `colorOrder`, brightness, and smoothing.
  - Call `begin(config)`, set a few states/modes, and call `tick(nowMs)` from a
    loop.
  - Use `nowMs = esp_timer_get_time() / 1000`.
  - Use `vTaskDelay(pdMS_TO_TICKS(10))` in the example loop only.
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

## Ordered Checklist

1. Exclude `src/StatusLedBackendIdf.cpp` from IDF v6 builds.
2. Select exactly one IDF backend macro in CMake.
3. Fix `src/StatusLedBackendIdf5.cpp` payload lifetime with backend-owned
   storage or a blocking wait before `show()` returns.
4. Make RMT done callback state synchronization safe; `volatile bool` is not a
   sufficient cross-context contract.
5. Add root `CMakeLists.txt` and optional `idf_component.yml`.
6. Add a minimal IDF example using `esp_timer`.
7. Build null-backend host/unit tests.
8. Build IDF RMT backend for `esp32s2` and `esp32s3`.
9. Build existing Arduino examples to verify compatibility.
10. Run WS2812 hardware smoke, busy-path, and cleanup tests.
