# StatusLED

Non-blocking status LED engine for ESP32-S2/S3 driving 1..10 WS2812-class
(NeoPixel) LEDs. The animation core is framework-neutral; the output backend
(RMT driver) is selected at compile time for Arduino-ESP32 or pure ESP-IDF.

Typical use: a couple of status LEDs inside a larger firmware (connectivity,
health, error blips), plus the bundled CLI example for bench-testing a board.

PlatformIO package name: `status-led`.

## Quickstart (bench CLI)

```bash
# ESP32-S3, Arduino core 3.x / IDF 5.x RMT backend
pio run -e cli_esp32s3_idf5 -t upload
pio device monitor -e cli_esp32s3_idf5
```

Type `help` in the monitor. On Windows use `.\scripts\pio.cmd` instead of `pio`.

## Using the Library in Your Firmware

### PlatformIO (Arduino-ESP32)

```ini
lib_deps =
  https://github.com/janhavelka/StatusLED.git#v1.4.0
build_flags =
  -DSTATUSLED_BACKEND_IDF5_WS2812=1   ; Arduino core 3.x (IDF 5.x)
  ; -DSTATUSLED_BACKEND_IDF_WS2812=1  ; Arduino core 2.x (IDF 4.4)
```

Exactly one `STATUSLED_BACKEND_*` macro must be `1`; the build fails otherwise.
For host/native test environments use `-DSTATUSLED_BACKEND_NULL=1`.

### ESP-IDF component

The repository root is an ESP-IDF component (`CMakeLists.txt`,
`idf_component.yml`), supported on ESP-IDF 5.3 and newer, 6.x included. Add it
under `components/` or point `EXTRA_COMPONENT_DIRS` at it, then
`REQUIRES StatusLED` from your `main` component. The component compiles only the engine and the IDF5 RMT backend and
defines `STATUSLED_BACKEND_IDF5_WS2812=1` publicly. Legacy RMT and NeoPixelBus
are never compiled in an ESP-IDF build.

Note: ESP-IDF names a component after its directory, so the checkout must be
named `StatusLED` (or adjust the `REQUIRES` entry).

### Minimal usage

```cpp
#include "StatusLed/StatusLed.h"

StatusLed::StatusLed leds;

void setup() {
  StatusLed::Config cfg;
  cfg.dataPin = 48;
  cfg.ledCount = 2;
  cfg.colorOrder = StatusLed::ColorOrder::GRB;
  cfg.globalBrightness = 64;

  auto st = leds.begin(cfg);
  if (!st.ok()) {
    // handle error: st.code / st.msg / st.detail
  }

  leds.setPreset(0, StatusLed::StatusPreset::Connecting);
  leds.setPreset(1, StatusLed::StatusPreset::Ready);
}

void loop() {
  leds.tick(millis());
  // event blip: 300 ms green double-blink, then back to whatever LED 1 showed
  // leds.setTemporaryPreset(1, StatusLed::StatusPreset::Success, 300);
}
```

## Supported Targets

| Board              | Environment                | Backend                          | Notes                  |
| ------------------ | -------------------------- | -------------------------------- | ---------------------- |
| ESP32-S3-DevKitC-1 | `cli_esp32s3_idf5`         | RMT v2 (Arduino core 3.x, IDF 5) | USB CDC, default bench |
| ESP32-S2-Saola-1   | `cli_esp32s2_idf5`         | RMT v2 (Arduino core 3.x, IDF 5) | USB CDC                |
| ESP32-S3-DevKitC-1 | `cli_esp32s3_idf`          | Legacy RMT (Arduino core 2.0.17, IDF 4.4) | USB CDC       |
| ESP32-S2-Saola-1   | `cli_esp32s2_idf`          | Legacy RMT (Arduino core 2.0.17, IDF 4.4) | USB CDC       |
| ESP32-S3 / S2      | `examples/espidf_basic`    | RMT v2 (pure ESP-IDF >= 5.3)     | Native `app_main` CLI  |
| host               | `native`                   | Null backend                     | Unit tests             |

NeoPixelBus environments are also provided (opt-in, Arduino core 2.x only):
`cli_esp32s3_neopixelbus`, `cli_esp32s2_neopixelbus`.

## API Overview

| Method                                     | Description                                  |
| ------------------------------------------ | -------------------------------------------- |
| `Status begin(const Config&)`              | Validate config, allocate backend, start     |
| `void tick(uint32_t now_ms)`               | Cooperative update, call from `loop()`       |
| `void end()`                               | Blank LEDs, release driver resources         |
| `Status setMode(i, mode[, params])`        | Set temporal pattern (intensity over time)   |
| `Status setColor(i, rgb)`                  | Set primary color                            |
| `Status setSecondaryColor(i, rgb)`         | Set alternate color (used by `Alternate`)    |
| `Status setPreset(i, preset)`              | Set semantic preset (mode + colors)          |
| `Status setDefaultPreset(i, preset)`       | Set resting preset (see below)               |
| `Status setTemporaryPreset(i, preset, ms)` | Show preset for `ms`, then revert            |
| `Status clearTemporary(i)`                 | Revert a temporary preset early              |
| `Status setBrightness(i, level)`           | Per-LED brightness (0..255)                  |
| `Status setGlobalBrightness(level)`        | Global brightness scale (0..255)             |
| `Status setAllPreset/Mode/Color(...)`      | Apply to all configured LEDs                 |
| `Status clear()`                           | All LEDs off, all per-LED state reset        |
| `void forceRefresh()`                      | Retransmit the current frame on next tick    |
| `Status getLedSnapshot(i, out)`            | Read current LED state                       |
| `const Config& config()`                   | Configuration accepted by `begin()`          |
| `Status lastStatus()`                      | Last status of a fallible operation          |

All setters return `NOT_INITIALIZED` before `begin()` and `INVALID_CONFIG` on a
bad index, mode, or preset. `tick()` records backend transmit failures in
`lastStatus()`; poll it if you need driver health.

### Layers: mode, color, preset, temporary, default

- **Mode** is the temporal pattern (blink, pulse, ...). **Color** is set
  separately. Both are per LED.
- **Preset** sets mode and colors in one call and is remembered as the LED's
  current preset. `setMode`/`setColor` afterwards mark the preset as `Off`
  (custom state).
- **Temporary preset** overlays the LED for a duration. It activates on the next
  `tick()`, snapshots the state below it (mode, params, colors, preset) and
  restores that state when the duration elapses or `clearTemporary()` is called.
  Calling `setTemporaryPreset()` again while active just replaces the overlay
  and restarts the timer; the snapshot below is kept. Any explicit state change
  cancels it instead: `setPreset`, `setMode`, `setColor`, `setSecondaryColor`,
  the `setAll*` calls, `clearTemporary` and `clear`. Per-LED brightness is not
  part of the snapshot, so `setBrightness` during an overlay survives the revert.
- **Default preset** is applied immediately only if the LED is idle (`Off` mode,
  no preset, no temporary preset). Otherwise it is just stored and reported via
  `LedSnapshot`; it never overrides a running mode and is not restored when a
  temporary preset expires.

## Config

```cpp
struct Config {
  int dataPin = -1;                         // WS2812 data GPIO, required
  uint8_t ledCount = 0;                     // 1..10
  ColorOrder colorOrder = ColorOrder::GRB;  // wire byte order: GRB (WS2812) or RGB
  uint8_t rmtChannel = 0;                   // 0..3, legacy IDF and NeoPixelBus only
  uint8_t globalBrightness = 255;           // 0..255
  uint16_t smoothStepMs = 20;               // 5..1000, update period of smooth modes
};
```

## Error Model

```cpp
struct Status {
  Err code;         // OK, INVALID_CONFIG, RESOURCE_BUSY, NOT_INITIALIZED, HARDWARE_FAULT, ...
  int32_t detail;   // raw driver error (esp_err_t) or offending value
  const char* msg;  // static string literal, never heap
};
```

`status.ok()` for success, `status.inProgress()` for a transient
`RESOURCE_BUSY`. `Status::Ok()`, `Status::Error(...)` and the free `Ok()` /
`Error(...)` helpers construct values.

## Modes

Modes define intensity over time; the color is configured separately.

| Mode                          | Behaviour                                   | `ModeParams` used         |
| ----------------------------- | ------------------------------------------- | ------------------------- |
| `Off`, `Solid`, `Dim`         | Static 0 / 255 / ~19 %                      | none                      |
| `BlinkSlow`, `BlinkFast`, `Strobe`, `Beacon` | On for `onMs`, off for the rest of `periodMs` | `periodMs`, `onMs` |
| `FadeIn`, `FadeOut`           | One-shot ramp `minLevel`->`maxLevel` (or reverse), then hold | `riseMs` / `fallMs`, `minLevel`, `maxLevel` |
| `PulseSoft`, `PulseSharp`, `Breathing`, `Throb` | Continuous triangle wave between `minLevel` and `maxLevel` (soft variants are eased) | `periodMs`, `minLevel`, `maxLevel` |
| `DoubleBlink`, `TripleBlink`, `Heartbeat`, `Alternate`, `SOS` | Fixed step tables (Alternate toggles primary/secondary color; SOS is `...---...`, 3400 ms per cycle) | none (fixed timing) |
| `FlickerCandle`, `Glitch`     | Pseudo-random (per-LED LFSR)                | none                      |

`StatusLed::getModeDefaults(mode)` returns the defaults used by
`setMode(i, mode)`; pass a modified copy to `setMode(i, mode, params)`.

## Presets

| Preset        | Mode        | Color        |
| ------------- | ----------- | ------------ |
| `Off`         | Off         | -            |
| `Ready`       | Solid       | Green        |
| `Busy`        | PulseSoft   | Orange       |
| `Warning`     | BlinkSlow   | Amber        |
| `Error`       | BlinkFast   | Red          |
| `Critical`    | Strobe      | Red          |
| `Updating`    | Breathing   | Cyan         |
| `Info`        | Solid       | Blue         |
| `Maintenance` | DoubleBlink | Purple       |
| `AlarmPolice` | Alternate   | Red / Blue   |
| `HazardAmber` | DoubleBlink | Amber        |
| `Success`     | DoubleBlink | Green        |
| `Connecting`  | PulseSoft   | Blue         |
| `LowBattery`  | Beacon      | Red          |

## Backends and RMT Safety

Arduino-ESP32 aborts at boot if the legacy RMT driver and the RMT v2 driver are
both linked. Backend selection is therefore compile-time only, so exactly one
driver family ends up in a binary.

| Macro                             | Driver                                | Where it works                          |
| --------------------------------- | ------------------------------------- | --------------------------------------- |
| `STATUSLED_BACKEND_IDF5_WS2812=1` | `driver/rmt_tx.h` (RMT v2)            | Arduino core 3.x, ESP-IDF 5.1+ and 6.x  |
| `STATUSLED_BACKEND_IDF_WS2812=1`  | `driver/rmt.h` (legacy)               | Arduino core 2.x (IDF 4.4)              |
| `STATUSLED_BACKEND_NEOPIXELBUS=1` | NeoPixelBus 2.7.6 (legacy RMT inside) | Arduino core 2.x                        |
| `STATUSLED_BACKEND_NULL=1`        | none                                  | host tests                              |

`Config::rmtChannel` selects the channel for the legacy and NeoPixelBus
backends. The RMT v2 backend lets the driver allocate a channel and ignores it.

The two RMT backends encode WS2812B timing themselves (T0H 0.40 us, T0L
0.85 us, T1H 0.80 us, T1L 0.45 us at 40 MHz RMT resolution), append a 300 us
latch gap after every frame, and keep the transmit buffer in backend-owned
storage until the transfer completes. The NeoPixelBus backend delegates all of
that to NeoPixelBus, which uses the same bit timings and a 300 us reset.
`show()` returns `RESOURCE_BUSY` while a previous frame is still on
the wire; the engine keeps the frame dirty and retries on the next `tick()`.

The 300 us gap matters: WorldSemi raised the required reset time from 50 us to
280 us for the WS2812B-V5 and WS2812B-2020 generations. With a shorter gap those
parts treat the next frame as a continuation of the previous one and pixel data
shifts down the chain instead of latching.

### Hardware notes

- **Logic level.** WS2812/WS2812B parts made before about 2017 specify
  `VIH = 0.7 x VDD`, which is 3.5 V on a 5 V supply and above what an
  ESP32-S2/S3 pin drives. WS2812B-V5 and WS2812C-2020 relaxed this to 2.7 V and
  work directly on 3.3 V. For older stock use a level shifter, a 74AHCT buffer,
  or drop the first LED's supply with a series diode.
- **Data line at rest.** `end()` blanks the LEDs and leaves the data pin driven
  low, which is the idle state WS2812 parts expect.
- **Flash writes.** The RMT driver refills its buffer from an interrupt that is
  not IRAM-resident, so a concurrent NVS or filesystem write can disturb a frame
  in flight. Schedule LED transmissions away from flash writes when a transient
  color glitch is unacceptable; reserving enough RMT memory for a complete frame
  would consume most or all RMT blocks at the 10-LED limit.

### Known limitations

- The current 0-bit high time is 400 ns. It is field-proven on the tested LEDs,
  but exceeds the 380 ns maximum in newer WorldSemi datasheets. Validate the
  selected LED revision on hardware before deployment.
- `lastStatus()` is a last-call result, not a sticky hardware-health record. A
  later successful API call replaces an earlier transmit failure.
- The maximum LED count is fixed at 10. A persistently failing installed backend
  is retried on every `tick()` while the frame remains dirty.
- Build the Arduino core 2.x and core 3.x environment families separately when
  using one shared PlatformIO core directory; switching families can cause large
  framework package copies on Windows.

## Runtime Model

- **Threading:** single-threaded, no internal tasks. Call every method from the
  same task (typically `loop()`), never from an ISR.
- **Timing:** `tick()` is bounded (a few microseconds per LED plus one
  non-blocking RMT submit). Blink and pattern steps are scheduled by deadline;
  smooth modes update every `smoothStepMs`. Wraparound of the 32-bit
  millisecond clock is handled.
- **Memory:** the backend object is allocated once in `begin()` and freed in
  `end()`. Zero allocation in `tick()` or any setter.
- **Retransmit policy:** a frame is sent only when a pixel value changed
  (static modes never retransmit, blink modes only on toggles, smooth modes on
  quantized steps) or after `forceRefresh()`.
- **Errors:** every fallible call returns `Status`; nothing is silent.

## Examples

| Example                       | Description                                        |
| ----------------------------- | -------------------------------------------------- |
| `examples/01_status_led_cli`  | Arduino serial CLI: full API, diagnostics, stress  |
| `examples/espidf_basic`       | Same CLI on native ESP-IDF (`app_main`, `esp_timer`, POSIX stdin) |

Both CLIs accept the same commands and the same mode/preset names. Useful
diagnostics: `help`, `version`, `info`, `status`, `config`, `last`.

```bash
# Arduino CLI builds
pio run -e cli_esp32s2_idf5 -t upload && pio device monitor -e cli_esp32s2_idf5
pio run -e cli_esp32s3_idf  -t upload && pio device monitor -e cli_esp32s3_idf

# Native ESP-IDF CLI (run from examples/espidf_basic with idf.py on PATH)
idf.py set-target esp32s3
idf.py build flash monitor
```

Windows note for the IDF5 envs: if package extraction fails because of long
paths, enable long paths in Windows or use a short PlatformIO core dir:

```powershell
$env:PLATFORMIO_CORE_DIR = "C:\p"
python -m platformio run -e cli_esp32s3_idf5
```

## Tests

Host-based unit tests for timing and state transitions:

```bash
pio test -e native
```

Requires a host C++ compiler (GCC/Clang). On Windows, install MinGW-w64 and make
sure `g++` is in `PATH`.

## Versioning

The version lives in `library.json` / `idf_component.yml`. The PlatformIO
pre-build script regenerates `include/StatusLed/Version.h` from it; the file is
also tracked so that ESP-IDF consumers (which do not run the script) get a
valid header.

```cpp
#include "StatusLed/Version.h"
Serial.println(StatusLed::VERSION);       // "1.4.0"
Serial.println(StatusLed::VERSION_FULL);  // "1.4.0 (commit, date time)"
```

## API Documentation

```bash
doxygen Doxyfile   # output in docs/doxygen/html
```

## Adding New Modes or Presets

1. Add the enum value in `include/StatusLed/StatusLed.h`.
2. Add behaviour in `src/StatusLed.cpp`: a pattern table or a case in
   `updateLed()`, defaults in `getModeDefaults()`, or a row in `kPresets`.
3. Add the name to both CLI name tables (`examples/01_status_led_cli/main.cpp`,
   `examples/espidf_basic/main/main.cpp`).
4. Update the mode/preset tables above and add a test in `test/`.

## Project Structure

```
include/StatusLed/      Public API (Config.h, Status.h, StatusLed.h,
                        BackendConfig.h, Version.h)
src/StatusLed.cpp       Animation engine (framework-neutral)
src/StatusLedBackend*.cpp
                        One output backend per file (Idf, Idf5, NeoPixelBus, Null)
examples/01_status_led_cli/   Arduino CLI
examples/espidf_basic/        Native ESP-IDF CLI
examples/common/              Example-only helpers (BoardPins.h, Log.h)
test/                   Host unit tests (Unity)
scripts/                Version generator, text-integrity check, pio wrapper
docs/                   Audit report
CMakeLists.txt, idf_component.yml   ESP-IDF component definition
```

## See Also

- `CHANGELOG.md` - version history
- `docs/CODE_AUDIT.md` - audit findings that remain open, with proposals
- `SECURITY.md` - security policy
- `AGENTS.md` - engineering guidelines for contributors and AI agents
