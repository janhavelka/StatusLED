# Backend Hardening Audit Report

Date: 2026-02-02

## Scope

Verify backend selection hardening and Arduino-ESP32 v3.x safety for StatusLED. Ensure mutually exclusive backend selection, isolation of backend headers and dependencies, PlatformIO env separation, documentation clarity, and build/test matrix pass.

## Investigation Answers

1. Backend selection enforcement  
   Enforced in `include/StatusLed/BackendConfig.h`, included by `include/StatusLed/StatusLed.h`. It emits compile-time errors if none or more than one of `STATUSLED_BACKEND_IDF_WS2812`, `STATUSLED_BACKEND_NEOPIXELBUS`, `STATUSLED_BACKEND_NULL` is defined. It also exposes `StatusLed::kSelectedBackend`.

2. Backend header isolation  
   Backend implementations are split into `src/StatusLedBackendIdf.cpp`, `src/StatusLedBackendNeoPixelBus.cpp`, and `src/StatusLedBackendNull.cpp`. Each file includes its backend headers only inside its `#if` guard. `src/StatusLed.cpp` includes no backend headers.

3. IDF backend builds without NeoPixelBus  
   `pio run -e cli_esp32s3_idf` and `pio run -e cli_esp32s2_idf` both succeed with no NeoPixelBus dependency pulled.

4. NeoPixelBus backend builds only when selected  
   `pio run -e cli_esp32s3_neopixelbus` and `pio run -e cli_esp32s2_neopixelbus` succeed and pull NeoPixelBus only in those envs.

5. RMT legacy vs NG conflict mitigation  
   Potential conflict triggers include `neopixelWrite()`, `RGB_BUILTIN`, or other libraries that pull the next-gen RMT driver. Documentation instructs using the IDF backend as the default and avoiding mixed usage if conflicts are observed.

6. Default backend is the recommended safe backend  
   `platformio.ini` default env is `cli_esp32s3_idf`. README states IDF backend is recommended and NeoPixelBus is opt-in.

7. Agent rules filename consistency  
   `AGENTS.md` is the canonical file and README references `AGENTS.md`. No mismatch found.

8. README backend selection section  
   README includes an explicit backend selection section with env list, recommended IDF default, and RMT conflict warning with remediation guidance.

9. Build/test matrix status  
   All required commands executed and passed. See Build/Test Results.

## Changes Made

1. `include/StatusLed/BackendConfig.h`  
   Added authoritative compile-time backend selection guards and `kSelectedBackend`.

2. `include/StatusLed/StatusLed.h`  
   Now includes `BackendConfig.h` and removes inline backend guard logic.

3. `src/StatusLedBackend.h`  
   Added internal backend interface plus `createBackend()` / `destroyBackend()` declarations.

4. `src/StatusLedInternal.h`  
   Added shared `mapColorOrder()` helper for engine and backends.

5. `src/StatusLedBackendIdf.cpp`  
   Moved IDF backend implementation to its own translation unit with guarded IDF includes.

6. `src/StatusLedBackendNeoPixelBus.cpp`  
   Moved NeoPixelBus backend implementation to its own translation unit with guarded NeoPixelBus includes. Fixed a bug by storing `config.ledCount` in the backend before allocating the bus.

7. `src/StatusLedBackendNull.cpp`  
   Moved null backend implementation to its own translation unit with guarded code.

8. `src/StatusLed.cpp`  
   Removed backend implementations and backend headers. Uses `createBackend()` / `destroyBackend()` factory functions.

## Build/Test Results

1. `pio run -e cli_esp32s3_idf`  
   Result: PASS

2. `pio run -e cli_esp32s2_idf`  
   Result: PASS

3. `pio run -e cli_esp32s3_neopixelbus`  
   Result: PASS

4. `pio run -e cli_esp32s2_neopixelbus`  
   Result: PASS

5. `pio test -e native`  
   Result: PASS  
   Note: Executed with MinGW-w64 (`gcc`/`g++`) on PATH via WinLibs installation. On Windows, a host compiler is required for native tests.

## Residual Risks

1. Runtime RMT conflict can still occur if user code or other libraries pull the next-gen RMT driver (for example `neopixelWrite()` or `RGB_BUILTIN`) while using a legacy RMT backend.

2. NeoPixelBus backend remains opt-in; if users select it and also use NG-based helpers in their application, conflicts may still occur.

3. Host tests depend on a system C++ compiler being available and on PATH.

## Next Steps for Hardware Smoke Tests

1. Flash `cli_esp32s3_idf` on ESP32-S3 and verify boot, no aborts, and LED output for multiple modes.

2. Flash `cli_esp32s2_idf` on ESP32-S2 and verify boot, no aborts, and LED output for multiple modes.

3. If NeoPixelBus is required in production, flash `cli_esp32s3_neopixelbus` and `cli_esp32s2_neopixelbus` and verify no RMT driver conflicts when combined with the full application.
