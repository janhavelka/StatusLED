# StatusLED

Non-blocking status LED engine for ESP32-S2/S3 driving WS2812-class (NeoPixel)
LEDs: up to 10 by default, configurable to 255. The animation core is
framework-neutral; the output backend
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
  https://github.com/janhavelka/StatusLED.git#v1.5.0
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
| `uint32_t outputErrorCount()`               | Persistent count of failed output submissions |
| `Status lastOutputStatus()`                 | Most recent output failure since initialization |

All setters return `NOT_INITIALIZED` before `begin()` and `INVALID_CONFIG` on a
bad index, mode, or preset. `tick()` records backend transmit failures in
`lastStatus()`, which a later successful setter can replace. For output health,
poll `outputErrorCount()` and `lastOutputStatus()` instead. The counter saturates
at `UINT32_MAX`; successful calls and frames preserve this history. A `begin()`
attempt that passes engine validation clears it before backend initialization,
even if that initialization fails. Engine validation errors preserve the running
instance and its history. `RESOURCE_BUSY` is not an error.
After a non-busy failure, output polling and retry pause for 100 ms while
animations continue and pending updates coalesce.

### Layers: mode, color, preset, temporary, default

- **Mode** is the temporal pattern (blink, pulse, ...). **Color** is set
  separately. Both are per LED.
- **Preset** sets mode and colors in one call and is remembered as the LED's
  current preset. `setMode`/`setColor` afterwards mark the preset as `Off`
  (custom state).
- **Temporary preset** overlays the LED for a duration. It activates on the next
  `tick()`, snapshots the state below it (mode, params, colors, preset and animation progress) and
  restores that state when the duration elapses or `clearTemporary()` is called.
  The underlying animation pauses during the overlay, including blink/pattern
  phase and the time remaining until its next step; completed fades stay complete.
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
  uint8_t ledCount = 0;                     // 1..kMaxLedCount (default maximum 10)
  ColorOrder colorOrder = ColorOrder::GRB;  // wire byte order: GRB (WS2812) or RGB
  uint8_t rmtChannel = 0;                   // 0..3, legacy IDF and NeoPixelBus only
  uint8_t globalBrightness = 255;           // 0..255
  uint16_t smoothStepMs = 20;               // 5..1000, update period of smooth modes
  bool rmtFullFrameBuffer = false;          // reserve a complete frame in RMT memory
};
```

Set `STATUSLED_MAX_LED_COUNT` to 1..255 as a **build-wide** compiler flag to
change capacity; invalid values fail compilation. It sizes the engine and backend
arrays, so application and library translation units must agree. Do not define it
in just one source file. For PlatformIO add
`-DSTATUSLED_MAX_LED_COUNT=12` to `build_flags`. In an ESP-IDF application's
top-level CMake file, after `project(...)`, use:

```cmake
idf_component_get_property(statusled_lib StatusLED COMPONENT_LIB)
target_compile_definitions(${statusled_lib} PUBLIC STATUSLED_MAX_LED_COUNT=12)
```

Larger capacities cost fixed RAM (about 24 KB for the legacy backend's item buffer alone at 255 LEDs).

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
| `STATUSLED_BACKEND_IDF5_WS2812=1` | `driver/rmt_tx.h` (RMT v2)            | Arduino core 3.x, ESP-IDF 5.3+ and 6.x  |
| `STATUSLED_BACKEND_IDF_WS2812=1`  | `driver/rmt.h` (legacy)               | Arduino core 2.x (IDF 4.4)              |
| `STATUSLED_BACKEND_NEOPIXELBUS=1` | NeoPixelBus 2.7.6 (legacy RMT inside) | Arduino core 2.x                        |
| `STATUSLED_BACKEND_NULL=1`        | none                                  | host tests                              |

`Config::rmtChannel` selects the channel for the legacy and NeoPixelBus
backends. The RMT v2 backend lets the driver allocate a channel and ignores it.

The two RMT backends share WS2812 timing constants (T0H 0.325 us, T0L
0.925 us, T1H 0.80 us, T1L 0.45 us at 40 MHz RMT resolution), append a 300 us
latch gap after every frame, and keep the transmit buffer in backend-owned
storage until the transfer completes. The NeoPixelBus backend delegates all of
that to the pinned NeoPixelBus 2.7.6 implementation, whose waveform is unchanged.
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
- **Flash writes.** Default one-block streaming can be interrupted when flash
  operations disable the cache: interrupt allocation flags, not just code
  placement in IRAM, determine whether refills continue. Set
  `Config::rmtFullFrameBuffer = true` to keep the entire frame, 300 us reset and
  stop marker in peripheral memory. This avoids timing-critical refills at the
  cost of adjacent RMT blocks. Ten LEDs need six 48-word blocks on S3 and all
  four 64-word blocks on S2. Two LEDs need two blocks on S3 or one on S2.
  Oversized frames or an unsuitable legacy start channel return `INVALID_CONFIG`;
  unavailable resources return an error rather than silently falling back.
  Coordinate legacy RMT resource initialization and shared interrupt policy with
  other users; do not allocate overlapping channels concurrently. Full-frame mode is ignored by NeoPixelBus
  and Null. Larger strips can still use default streaming.
- **Cache-safe ESP-IDF builds.** The IDF5 callbacks/encoder functions reside in
  IRAM, and both RMT backend objects are allocated once in internal RAM. Enable
  `CONFIG_RMT_ISR_IRAM_SAFE` on IDF 5.3/5.4 or
  `CONFIG_RMT_TX_ISR_CACHE_SAFE` on 5.5+ when refills must run during flash writes.
  These SDK settings require rebuilding ESP-IDF; a compiler flag cannot change
  the prebuilt Arduino SDK. Internal allocation preserves support for multiple
  StatusLed instances and does not allocate during normal output.

### Known limitations

- T0H 325 ns addresses the newer 380 ns maximum while retaining a 1.25 us bit
  period. The unchanged one-bit waveform is not universally compliant with every
  published WS2812x/SK6812 revision: their low-time windows conflict, and older
  SK6812 revisions specify a shorter high pulse. Qualify the exact LED revision
  electrically.
- `end()` attempts to blank the strip within bounded waits, then releases the
  driver and drives the pin low. Hardware/driver failure can prevent blanking.
- Default streaming still requires scheduling away from flash operations when
  the SDK does not enable cache-safe RMT interrupts. Use full-frame buffering
  where the peripheral memory budget permits it.
- Concurrent projects installing different Arduino framework versions can replace
  a shared framework directory while another build uses it. Serialize those
  builds/installations or give concurrently active platform families separate
  short `PLATFORMIO_CORE_DIR` paths. The wrapper still uses the same installed
  VS Code-managed Core executable. Matching installed versions are resolved by
  metadata; a sequential environment switch does not inherently copy files.

## Runtime Model

- **Threading:** single-threaded, no internal tasks. Call every method from the
  same task (typically `loop()`), never from an ISR.
- **Timing:** `tick()` is bounded (a few microseconds per LED plus one
  non-blocking RMT submit). Blink and pattern steps are scheduled by deadline;
  smooth modes update every `smoothStepMs`. Wraparound of the 32-bit
  millisecond clock is handled when successive calls are less than
  0x80000000 ms apart.
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
`info` includes persistent output health. `begin [pin] [count] [grb|rgb] [rmt]
[smooth_ms] [full_frame]` accepts an optional final 0/1 switch for full-frame RMT
buffering (default 0). For example, `begin 21 2 grb 0 20 1` enables it on two
LEDs at GPIO21. Both CLIs reject signed, overflowing or malformed unsigned
numbers and invalid color-order names before changing configuration.

```bash
# Arduino CLI builds
pio run -e cli_esp32s2_idf5 -t upload && pio device monitor -e cli_esp32s2_idf5
pio run -e cli_esp32s3_idf  -t upload && pio device monitor -e cli_esp32s3_idf

# Native ESP-IDF CLI (run from examples/espidf_basic with idf.py on PATH)
idf.py set-target esp32s3
idf.py build flash monitor
```

Windows note: use separate short storage directories if concurrent projects
install different framework versions, or if package extraction hits path limits:

```powershell
$env:PLATFORMIO_CORE_DIR = "C:\sl-pio2"
.\scripts\pio.cmd run -e cli_esp32s3_idf
$env:PLATFORMIO_CORE_DIR = "C:\sl-pio3"
.\scripts\pio.cmd run -e cli_esp32s3_idf5
```

These commands use the existing VS Code-managed executable; they do not install
another PlatformIO Core. The storage directories duplicate packages/toolchains,
so allow additional disk space. For a single platform family, one short path is
enough:

```powershell
$env:PLATFORMIO_CORE_DIR = "C:\p"
.\scripts\pio.cmd run -e cli_esp32s3_idf5
```

## Tests

Host-based unit tests for timing and state transitions:

```bash
pio test -e native -e native_max
```

Both environments run 60 tests. Independent compile-time expectations require
capacity 10 in `native` and 255 in `native_max`; removing or mistyping the latter's
`STATUSLED_MAX_LED_COUNT` flag fails the build. Host-only controls exercise
counter saturation and backend initialization failure without hardware or waits.

Requires a host C++ compiler (GCC/Clang). On Windows, install MinGW-w64 and make
sure `g++` is in `PATH`.

## Versioning

The version lives in `library.json` / `idf_component.yml`. The PlatformIO
pre-build script regenerates `include/StatusLed/Version.h` from it; the file is
also tracked so that ESP-IDF consumers (which do not run the script) get a
valid header.

```cpp
#include "StatusLed/Version.h"
Serial.println(StatusLed::VERSION);       // "1.5.0"
Serial.println(StatusLed::VERSION_FULL);  // "1.5.0 (commit, date time)"
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
examples/common/              Example-only helpers (BoardPins.h, CliParse.h, Log.h)
test/                   Host unit tests (Unity)
scripts/                Version generator, text-integrity check, pio wrapper
docs/                   Historical audit and implementation review
CMakeLists.txt, idf_component.yml   ESP-IDF component definition
```

## See Also

- `CHANGELOG.md` - version history
- [docs/CODE_AUDIT.md](docs/CODE_AUDIT.md) - historical audit of `a7e0e4e`,
  whose findings were subsequently addressed
- [docs/CODE_AUDIT_REVIEW.md](docs/CODE_AUDIT_REVIEW.md) - implementation verdicts,
  verification evidence and remaining hardware qualification
- `SECURITY.md` - security policy
- `AGENTS.md` - engineering guidelines for contributors and AI agents
