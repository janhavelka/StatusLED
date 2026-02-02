# StatusLED

Production-grade status LED subsystem for ESP32-S2/S3 (WS2812/NeoPixel-class) using Arduino + PlatformIO.

## Quickstart

```bash
# Upload CLI example (S3, IDF backend)
pio run -e cli_esp32s3_idf -t upload
pio device monitor -e cli_esp32s3_idf
```

## Supported Targets

| Board                 | Environment       | Notes            |
| --------------------- | ----------------- | ---------------- |
| ESP32-S3-DevKitC-1    | `cli_esp32s3_idf`  | USB CDC enabled  |
| ESP32-S2-Saola-1      | `cli_esp32s2_idf`  | USB CDC enabled  |

## Versioning

The library version is defined in `library.json`. A pre-build script generates `include/StatusLed/Version.h`.

```cpp
#include "StatusLed/Version.h"

Serial.println(StatusLed::VERSION);
Serial.println(StatusLed::VERSION_FULL);
```

## API Overview

```cpp
#include "StatusLed/StatusLed.h"

StatusLed::StatusLed leds;

void setup() {
  StatusLed::Config cfg;
  cfg.dataPin = 48;
  cfg.ledCount = 3;
  cfg.colorOrder = StatusLed::ColorOrder::GRB;
  cfg.rmtChannel = 0;

  auto st = leds.begin(cfg);
  if (!st.ok()) {
    // handle error
  }

  leds.setPreset(0, StatusLed::StatusPreset::Ready);
}

void loop() {
  leds.tick(millis());
}
```

### Core Methods

| Method                                     | Description                                  |
| ------------------------------------------ | -------------------------------------------- |
| `Status begin(const Config&)`              | Initialize with configuration                |
| `void tick(uint32_t now_ms)`               | Cooperative update, call from `loop()`       |
| `void end()`                               | Stop and release resources                   |
| `Status setMode(i, mode[, params])`        | Set LED mode (temporal behavior)             |
| `Status setColor(i, rgb)`                  | Set LED primary color                        |
| `Status setSecondaryColor(i, rgb)`         | Set alternate color for composite modes      |
| `Status setPreset(i, preset)`              | Set semantic preset                          |
| `Status setDefaultPreset(i, preset)`       | Set default preset                           |
| `Status setTemporaryPreset(i, preset, ms)` | Temporary preset then revert                 |
| `Status setBrightness(i, level)`           | Per-LED brightness (0..255)                  |
| `Status setGlobalBrightness(level)`        | Global brightness scale (0..255)             |
| `Status getLedSnapshot(i, out)`            | Read current LED state                       |

## Config

```cpp
struct Config {
  int dataPin = -1;            // WS2812 data pin
  uint8_t ledCount = 0;        // 1..10 (max 10)
  ColorOrder colorOrder = ColorOrder::GRB;  // GRB or RGB
  uint8_t rmtChannel = 0;      // 0..3 for ESP32-S2/S3
  uint8_t globalBrightness = 255;
  uint16_t smoothStepMs = 20;  // quantized smooth updates
};
```

## Error Model

All fallible operations return `Status`:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;  // static string only
};
```

`msg` must always be a static string literal (no heap allocation).

## Modes

Temporal intensity modes (color is configured separately):

- Off
- Solid
- Dim
- BlinkSlow
- BlinkFast
- DoubleBlink
- TripleBlink
- Beacon
- Strobe
- FadeIn
- FadeOut
- PulseSoft
- PulseSharp
- Breathing
- Heartbeat
- Throb
- FlickerCandle
- Glitch
- Alternate

Use `setMode(i, mode, params)` to override period, duty, and fade timings.

## Presets

Semantic presets (mode + color):

- Ready -> Solid Green
- Busy -> PulseSoft Orange
- Warning -> BlinkSlow Amber
- Error -> BlinkFast Red
- Critical -> Strobe Red
- Updating -> Breathing Cyan
- Info -> Solid Blue
- Maintenance -> DoubleBlink Purple
- AlarmPolice -> Alternate Red/Blue
- HazardAmber -> DoubleBlink Amber

## Backend Selection and RMT Safety

Arduino-ESP32 v3.x can abort at boot if legacy RMT and next-gen RMT drivers are
linked together. To reduce this risk, **IDF WS2812 is the recommended default**.

Choose a backend by selecting the matching PlatformIO environment:

- **IDF backend (recommended):** `cli_esp32s3_idf`, `cli_esp32s2_idf`
- **NeoPixelBus backend (opt-in):** `cli_esp32s3_neopixelbus`, `cli_esp32s2_neopixelbus`
- **Host tests:** `native` (uses `STATUSLED_BACKEND_NULL`)

If your application uses `neopixelWrite()` or board RGB helpers, prefer the IDF backend to
avoid legacy vs NG driver conflicts.

## Threading and Timing Model

- **Threading Model:** Single-threaded by default. No internal tasks.
- **Timing:** `tick()` completes in <1ms. Long operations split across calls.
- **Resource Ownership:** LED pin and RMT channel passed via Config. No hardcoded resources.
- **Memory:** All allocation in `begin()`. Zero allocation in `tick()`.
- **Error Handling:** All errors returned as Status. No silent failures.

## No Retransmit Behavior

- Static modes do not retransmit.
- Blink modes transmit only on on/off transitions.
- Smooth modes transmit only on quantized brightness steps.

## Examples

| Example                 | Description                                         |
| ----------------------- | --------------------------------------------------- |
| `01_status_led_cli`     | Interactive CLI with full API access + stress test  |

### Building Examples

```bash
# CLI (S2, IDF backend)
pio run -e cli_esp32s2_idf -t upload
pio device monitor -e cli_esp32s2_idf
```

## Tests

Host-based unit tests for timing and state transitions:

```bash
pio test -e native
```

Requires a host C++ compiler (GCC/Clang). On Windows, install MinGW-w64
(e.g., WinLibs) and ensure `gcc`/`g++` are in `PATH` (restart shell after install).

## Adding New Modes or Presets

1. Add a new `Mode` or `StatusPreset` in `include/StatusLed/StatusLed.h`.
2. Add behavior in `src/StatusLed.cpp` (`updateLed()` and presets table).
3. Update README mode/preset list.
4. Add or update tests in `test/`.

## Project Structure

```
include/StatusLed/   # Public headers (library API)
  |-- Config.h
  |-- Status.h
  |-- StatusLed.h
  |-- Version.h
src/
  |-- StatusLed.cpp
examples/
  |-- 01_status_led_cli/
  |-- common/
```

## See Also

- `CHANGELOG.md` - Version history
- `SECURITY.md` - Security policy
- `AGENTS.md` - AI agent guidelines
