# StatusLED Library: ESP-IDF 5.x RMT v2 Backend Migration Prompt

## Context for the AI Coder

You are working on the **StatusLED** library: `https://github.com/janhavelka/StatusLED.git` (tag `v1.1.0`).
Read the library's `AGENTS.md` fully before starting — it is binding.

This library drives WS2812 LEDs on ESP32-S2/S3 via the ESP-IDF RMT peripheral. The current IDF backend (`StatusLedBackendIdf.cpp`) uses the **legacy RMT driver** (`driver/rmt.h`) from ESP-IDF 4.4.x. This driver was removed in ESP-IDF 5.x.

The goal is to add a **new backend** that uses the ESP-IDF 5.x **RMT v2 driver** (`driver/rmt_tx.h` + `driver/rmt_encoder.h`), so the library works on both ESP-IDF 4.4.x and 5.x without breaking backward compatibility.

---

## Scope

**In scope:**
- New backend file: `src/StatusLedBackendIdf5.cpp`
- New compile-time macro: `STATUSLED_BACKEND_IDF5_WS2812`
- Update `include/StatusLed/BackendConfig.h` to include the new backend in the mutual-exclusion logic
- Update the library's `platformio.ini` with IDF 5.x test environments
- Tests, documentation, changelog

**Out of scope:**
- Changing the public API (`StatusLed.h`, `Config.h`, `Status.h`)
- Modifying the existing legacy backend (`StatusLedBackendIdf.cpp`)
- Changing the `BackendBase` interface
- Any runtime auto-detection (backend is compile-time only)

---

## Current Architecture

### Backend Interface (`src/StatusLedBackend.h`)
```cpp
struct BackendBase {
  virtual ~BackendBase() = default;
  virtual Status begin(const Config& config) = 0;
  virtual void end() = 0;
  virtual bool canShow() const = 0;
  virtual Status show(const RgbColor* frame, uint8_t count, ColorOrder order) = 0;
};

// Factory — exactly one backend is compiled in:
BackendBase* createBackend();
void destroyBackend(BackendBase* backend);
```

### Current Legacy Backend (`src/StatusLedBackendIdf.cpp`) — IDF 4.4.x
Uses these legacy APIs (all removed in IDF 5.x):
- `#include "driver/rmt.h"`
- `rmt_config_t`, `RMT_DEFAULT_CONFIG_TX()`
- `rmt_config()`, `rmt_driver_install()`, `rmt_driver_uninstall()`
- `rmt_wait_tx_done()`, `rmt_write_items()`
- `rmt_item32_t` (32-bit symbol struct)
- `rmt_channel_t` (enum-based channel selection)

### Backend Selection (`include/StatusLed/BackendConfig.h`)
Exactly one `STATUSLED_BACKEND_*` macro must be set to `1`. Others must be `0`.
Currently: `STATUSLED_BACKEND_IDF_WS2812`, `STATUSLED_BACKEND_NEOPIXELBUS`, `STATUSLED_BACKEND_NULL`.

### Config (`include/StatusLed/Config.h`)
```cpp
struct Config {
  int dataPin = -1;
  uint8_t ledCount = 0;
  ColorOrder colorOrder = ColorOrder::GRB;
  uint8_t rmtChannel = 0;        // Legacy: selects rmt_channel_t enum value
  uint8_t globalBrightness = 255;
  uint16_t smoothStepMs = 20;
};
```

**Note:** `rmtChannel` is used by the legacy backend to select a specific RMT channel by enum index. The IDF 5.x driver allocates channels dynamically via `rmt_new_tx_channel()` — the new backend should **ignore** `rmtChannel` (or document that it is unused in IDF 5.x mode). Do NOT change the Config struct — it must stay backward compatible.

---

## ESP-IDF 5.x RMT v2 API Reference

### Headers
```cpp
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
```

### Key Types
- `rmt_channel_handle_t` — opaque handle (replaces `rmt_channel_t` enum)
- `rmt_encoder_handle_t` — opaque encoder handle
- `rmt_symbol_word_t` — 32-bit symbol (replaces `rmt_item32_t`), fields: `duration0`, `level0`, `duration1`, `level1`

### Channel Lifecycle
```cpp
// Create
rmt_tx_channel_config_t tx_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = <gpio>,
    .mem_block_symbols = 64,             // minimum 64
    .resolution_hz = 40 * 1000 * 1000,   // 40 MHz (match legacy: 80MHz / clk_div=2)
    .trans_queue_depth = 1,
    .flags.invert_out = false,
    .flags.with_dma = false,
};
rmt_channel_handle_t tx_chan = NULL;
esp_err_t err = rmt_new_tx_channel(&tx_cfg, &tx_chan);

// Enable before first transmit
rmt_enable(tx_chan);

// Disable + delete on cleanup
rmt_disable(tx_chan);
rmt_del_channel(tx_chan);
```

### Encoder for WS2812
Use `rmt_new_bytes_encoder()` — it converts a byte stream into RMT symbols using configurable bit0/bit1 patterns:
```cpp
rmt_bytes_encoder_config_t bytes_cfg = {
    .bit0 = {
        .duration0 = 16,   // T0H: 0.4µs at 40MHz
        .level0 = 1,
        .duration1 = 34,   // T0L: 0.85µs at 40MHz
        .level1 = 0,
    },
    .bit1 = {
        .duration0 = 32,   // T1H: 0.8µs at 40MHz
        .level0 = 1,
        .duration1 = 18,   // T1L: 0.45µs at 40MHz
        .level1 = 0,
    },
    .flags.msb_first = true,
};
rmt_encoder_handle_t bytes_encoder = NULL;
rmt_new_bytes_encoder(&bytes_cfg, &bytes_encoder);
```

### Transmit
```cpp
rmt_transmit_config_t tx_config = {
    .loop_count = 0,          // no loop
    .flags.eot_level = 0,     // idle low after transmission
};
// payload = GRB byte array, payload_bytes = ledCount * 3
rmt_transmit(tx_chan, bytes_encoder, grb_data, led_count * 3, &tx_config);

// Non-blocking check if done:
esp_err_t wait_err = rmt_tx_wait_all_done(tx_chan, 0);  // timeout_ms=0 = poll
// wait_err == ESP_OK → done, ESP_ERR_TIMEOUT → still busy
```

### Cleanup
```cpp
rmt_disable(tx_chan);
rmt_del_encoder(bytes_encoder);
rmt_del_channel(tx_chan);
```

---

## Implementation Instructions

### 1. New File: `src/StatusLedBackendIdf5.cpp`

Create a new class `BackendIdf5Ws2812` implementing `BackendBase`.

**Guard:** `#if STATUSLED_BACKEND_IDF5_WS2812`

**`begin(config)`:**
1. Validate `config.dataPin` and `config.ledCount` (same validation as legacy backend).
2. Create TX channel via `rmt_new_tx_channel()`:
   - `resolution_hz = 40000000` (40 MHz — matches legacy 80MHz/div2)
   - `gpio_num` from config
   - `mem_block_symbols = 64`
   - `trans_queue_depth = 1`
   - No DMA (ESP32-S2 doesn't support RMT DMA)
3. Create bytes encoder via `rmt_new_bytes_encoder()` with WS2812 timing:
   - T0H = 16 ticks (0.4µs), T0L = 34 ticks (0.85µs)
   - T1H = 32 ticks (0.8µs), T1L = 18 ticks (0.45µs)
   - MSB first = true
4. Enable channel via `rmt_enable()`.
5. Store handles and `_count`, set `_installed = true`.
6. Return `Ok()` on success, `Status(Err::HARDWARE_FAULT, err, ...)` on failure.

**`end()`:**
1. If installed, best-effort blank (transmit zeroed GRB buffer).
2. `rmt_disable()` the channel.
3. `rmt_del_encoder()` the bytes encoder.
4. `rmt_del_channel()` the TX channel.
5. Null out handles, set `_installed = false`.

**`canShow()`:**
1. If not installed, return `false`.
2. `rmt_tx_wait_all_done(tx_chan, 0)` — return `true` if `ESP_OK`, `false` if `ESP_ERR_TIMEOUT`.

**`show(frame, count, order)`:**
1. Validate installed, frame not null, count in range.
2. Poll `rmt_tx_wait_all_done(tx_chan, 0)` — if `ESP_ERR_TIMEOUT`, return `Err::RESOURCE_BUSY`.
3. Build a GRB byte buffer (max 10 LEDs × 3 bytes = 30 bytes, stack-allocated) from `frame` using `mapColorOrder()`.
4. Call `rmt_transmit(tx_chan, bytes_encoder, grb_buf, count * 3, &tx_config)` with `loop_count = 0`.
5. Return `Ok()` on success.

**Key differences from legacy backend:**
- No manual `rmt_item32_t` array building — the bytes encoder handles bit-to-symbol conversion.
- No manual reset pulse — the `eot_level = 0` keeps the line low after TX, and the WS2812 reset time (≥50µs) is guaranteed by the gap between `show()` calls (library enforces `smoothStepMs ≥ 5ms`).
- Channel is opaque handle, not enum — `config.rmtChannel` is ignored (document this).

**Private members:**
```cpp
rmt_channel_handle_t _tx_chan = nullptr;
rmt_encoder_handle_t _bytes_encoder = nullptr;
bool _installed = false;
uint8_t _count = 0;
```

### 2. Update `include/StatusLed/BackendConfig.h`

Add guard for `STATUSLED_BACKEND_IDF5_WS2812`, add it to:
- Default `#ifndef` / `#define 0` block
- Value validation (`!= 0 && != 1` error)
- Mutual exclusion sum check
- `BackendType` enum: add `Idf5Ws2812 = 3`
- `kSelectedBackend` constexpr selection

### 3. Update `src/StatusLedBackendIdf5.cpp` Factory

At the bottom of the new file, provide the factory functions (same pattern as legacy):
```cpp
BackendBase* createBackend() {
  return new (std::nothrow) BackendIdf5Ws2812();
}
void destroyBackend(BackendBase* backend) {
  delete backend;
}
```

### 4. Update `platformio.ini`

Add IDF 5.x test environments. Use `pioarduino/espressif32` platform (or a newer `espressif32` that ships Arduino-ESP32 v3.x with IDF 5.x):
```ini
; Example IDF 5.x test env for S3
[env:cli_esp32s3_idf5]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/54.03.20/platform-espressif32.zip
framework = arduino
board = esp32-s3-devkitc-1
; ... (same board config as cli_esp32s3_idf)
build_flags =
  ${common.build_flags}
  -DSTATUSLED_BACKEND_IDF5_WS2812=1
  ; No need for -Wno-missing-field-initializers with the new API
```

**Important:** The IDF 4.4 environments keep using `STATUSLED_BACKEND_IDF_WS2812=1`. The IDF 5.x environments use `STATUSLED_BACKEND_IDF5_WS2812=1`. Both are never mixed.

### 5. Documentation

- Update `README.md` backend table with the new option.
- Update `AGENTS.md` if backend selection rules change.
- Update `CHANGELOG.md`: `## [1.2.0] - <date>` → `### Added` → "ESP-IDF 5.x RMT v2 backend (`STATUSLED_BACKEND_IDF5_WS2812`)".

### 6. Consumer Update (CO2Control)

After publishing a new StatusLED version with IDF 5.x support, the CO2Control `platformio.ini` would switch backend macros based on which platform is in use:

**For IDF 4.4.x builds (current):**
```ini
-DSTATUSLED_BACKEND_IDF_WS2812=1
```

**For IDF 5.x builds (future migration):**
```ini
-DSTATUSLED_BACKEND_IDF5_WS2812=1
```

The web libraries (`ESPAsyncWebServer@3.9.6`, `AsyncTCP@3.4.10`) are confirmed compatible with both Arduino Core 2.x (IDF 4.4) and 3.x (IDF 5.x) — no changes needed there.

---

## Testing Checklist

- [ ] `cli_esp32s3_idf` (IDF 4.4, legacy backend) still compiles and runs — no regression
- [ ] `cli_esp32s2_idf` (IDF 4.4, legacy backend) still compiles and runs — no regression
- [ ] `cli_esp32s3_idf5` (IDF 5.x, new backend) compiles without `-Wno-missing-field-initializers`
- [ ] `cli_esp32s2_idf5` (IDF 5.x, new backend) compiles and runs
- [ ] LED output is correct (color, brightness, all modes)
- [ ] `begin()` / `end()` / `begin()` cycle works (reinit-safe)
- [ ] No crash or reboot loop on boot
- [ ] `canShow()` correctly reports busy during transmission
- [ ] `show()` returns `RESOURCE_BUSY` when called faster than hardware can transmit
- [ ] Native test environment still passes (null backend)
- [ ] `BackendConfig.h` rejects multiple backends selected simultaneously
- [ ] No `-Wno-missing-field-initializers` needed for IDF 5.x builds

---

## Constraints (from AGENTS.md — binding)

- No logging in library code
- No heap allocation in `tick()` / `show()` steady-state (byte buffer is stack, max 30 bytes)
- All errors return `Status` with static string messages
- `begin()` / `tick()` / `end()` lifecycle
- No hardcoded pins
- Backend code must not pull conflicting driver APIs into the binary
- Prefer additive changes over breaking changes

---

## API Mapping Reference

| Legacy (IDF 4.4)                  | New (IDF 5.x)                              |
|-----------------------------------|---------------------------------------------|
| `#include "driver/rmt.h"`         | `#include "driver/rmt_tx.h"` + `"driver/rmt_encoder.h"` |
| `rmt_config_t`                    | `rmt_tx_channel_config_t`                   |
| `RMT_DEFAULT_CONFIG_TX()`         | Fill `rmt_tx_channel_config_t` manually     |
| `rmt_config()`                    | (not needed)                                |
| `rmt_driver_install()`            | `rmt_new_tx_channel()` + `rmt_enable()`     |
| `rmt_driver_uninstall()`          | `rmt_disable()` + `rmt_del_channel()`       |
| `rmt_write_items(items, N, ...)`  | `rmt_transmit(chan, encoder, data, len, &cfg)` |
| `rmt_wait_tx_done(chan, timeout)`  | `rmt_tx_wait_all_done(chan, timeout_ms)`     |
| `rmt_item32_t`                    | `rmt_symbol_word_t`                         |
| `rmt_channel_t` (enum)            | `rmt_channel_handle_t` (opaque handle)      |
| Manual bit→item encoding          | `rmt_new_bytes_encoder()` handles it        |
| Manual reset pulse item           | `eot_level = 0` + inter-frame gap           |
