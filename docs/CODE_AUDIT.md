# StatusLED Code Audit

Full review of the animation engine, all four output backends, the examples and
the documentation, checked against the WS2812 family datasheets and the ESP-IDF
RMT driver sources (legacy 4.4 and RMT v2 5.3 through 6.0).

Every finding below has been re-verified against the current tree and against
the primary sources a second time. Where the first pass was wrong, the
correction is stated inline rather than quietly dropped.

Findings whose fix was unambiguous were implemented in v1.4.0 and are listed in
`CHANGELOG.md`; the "Resolved" section at the end says where to check each one.
What remains here is **open**: issues whose fix is a trade-off, needs hardware
validation, or changes behaviour that is currently field-proven. Each entry
states the problem, the evidence, and one concrete proposal.

Verification state of the code as it stands: 43 host unit tests pass, the
legacy-RMT environment compiles, and the engine plus the RMT v2 backend compile
clean against ESP-IDF 5.4 headers with `-Wall -Wextra -Werror`. Nothing has been
tested on hardware.

Severity is about production impact, not effort.

## Open findings

| #  | Issue | Severity | Proposal |
| -- | ----- | -------- | -------- |
| 1  | RMT refill interrupt is masked during flash writes, glitching frames | High | Size the RMT memory blocks to hold the whole frame |
| 2  | Backend object is heap-allocated and could reach PSRAM | Low | Static placement, as a prerequisite for issue 1 |
| 3  | `T0H = 400 ns` exceeds the 220-380 ns window of every part made since 2017 | Medium | 325 ns after a scope check; `T1H` too if SK6812 matters |
| 4  | A transmit failure is erased by the next successful call | Medium | Add a sticky output-error counter |
| 5  | CI does not build the RMT v2 environments or the ESP-IDF component | Medium | Extend the build matrix |
| 6  | `kMaxLedCount` is hardcoded at 10 | Low | Overridable macro, with a range guard |
| 7  | A failing backend is retried every tick with no backoff | Low | Retry at a fixed slow interval |
| 8  | The two Arduino platforms contend for one framework package | Low | Separate PlatformIO core directories |

---

## 1. The RMT refill interrupt is masked during flash writes (High)

**Both RMT backends stream a frame out of RAM through a refill interrupt that is
not allocated as IRAM-safe. While the SPI flash cache is disabled, that
interrupt is masked.** Any NVS commit, LittleFS or SPIFFS write, or OTA download
therefore defers the refill past the WS2812 bit deadline, the RMT engine re-emits
the stale half of its buffer, and the strip shows corrupted colors until the next
frame. It is a visual glitch, not a crash.

Evidence:

- The legacy backend calls `rmt_driver_install(_channel, 0, 0)`. The third
  argument is `intr_alloc_flags`, so the shared RMT interrupt is allocated
  without `ESP_INTR_FLAG_IRAM`. The handler itself is `IRAM_ATTR`, which is not
  the same thing: only the allocation flag keeps an interrupt enabled while the
  cache is off.
- One frame is 241 RMT items for 10 LEDs, far more than the single memory block
  the driver configures by default (48 words on ESP32-S3, 64 on ESP32-S2), so it
  refills each half-block from a threshold interrupt.
- The deadline is the wire time of half a block, `mem_block_num * words / 2`
  bits at 1.25 us: **30 us on ESP32-S3**, 40 us on ESP32-S2. A flash write blocks
  for far longer.
- RMT v2 has the same structure. `CONFIG_RMT_TX_ISR_CACHE_SAFE`, split out of the
  now-deprecated `CONFIG_RMT_ISR_IRAM_SAFE` in ESP-IDF 5.5, defaults to `n`.

The Arduino-ESP32 3.3.11 configuration is worth stating precisely, because two of
its defaults look like they solve this and do not. It ships
`CONFIG_RMT_ENCODER_FUNC_IN_IRAM=y` and `CONFIG_RMT_TX_ISR_HANDLER_IN_IRAM=y`,
but leaves `CONFIG_RMT_TX_ISR_CACHE_SAFE` unset. Those two options only place
code in IRAM through the linker fragment; the interrupt allocation flag comes
solely from the cache-safe option. So the ISR code is resident and the interrupt
is still masked.

**Hardening the ISR naively turns a glitch into a crash.** Enabling
`CONFIG_RMT_TX_ISR_CACHE_SAFE` makes the ISR run with the cache off, and it would
then call this library's `encodeFrame()` and `resetFrameEncoder()`, which live in
flash. ESP-IDF's own encoders are decorated `RMT_ENCODER_FUNC_ATTR`; ours are not,
so the result is a cache-error panic. That same option also makes
`rmt_tx_register_event_callbacks()` reject a callback outside IRAM and a
`user_data` outside internal RAM, and `user_data` here is the backend object,
which ties this issue to issue 2.

**Proposal: remove the interrupt from the critical path rather than harden it.**
Give the channel enough RMT memory to hold the whole frame, so no refill happens.
This is reachable on both chips, which the first pass of this audit got wrong.

The legacy driver's own bound is `mem_block_num + channel <= SOC_RMT_CHANNELS_PER_GROUP`,
which is 8 on ESP32-S3 and 4 on ESP32-S2, not 4 everywhere. A TX channel may
therefore borrow the memory of the RX-only channels on ESP32-S3. Maximum for one
TX channel: **384 words on ESP32-S3** (8 x 48), **256 words on ESP32-S2** (4 x 64).
A 10-LED frame needs 242 words including the driver's stop marker, so:

| Chip | Words needed | Blocks | Setting | Fits |
| ---- | ------------ | ------ | ------- | ---- |
| ESP32-S3 | 242 | 6 x 48 = 288 | `mem_block_num = 6`, start at channel 0 | yes |
| ESP32-S2 | 242 | 4 x 64 = 256 | `mem_block_num = 4`, channel 0 | yes |

```cpp
// Legacy backend, in begin(): items = ledCount * 24 + 1, plus the stop marker.
const int words = (config.ledCount * kBitsPerPixel) + 2;
const int blocks = (words + SOC_RMT_MEM_WORDS_PER_CHANNEL - 1) /
                   SOC_RMT_MEM_WORDS_PER_CHANNEL;
if (config.rmtChannel + blocks > SOC_RMT_CHANNELS_PER_GROUP) {
  return Status(Err::INVALID_CONFIG, blocks, "not enough RMT memory blocks");
}
rmt_cfg.mem_block_num = blocks;
```

The RMT v2 backend takes the same number through `mem_block_symbols`, where the
driver rounds up to whole blocks and marks the absorbed channels occupied itself.
Note that its channel search only starts within the four TX candidates, while the
memory claim is tested against the whole group, which is why a 6-block channel
starting at 0 is legal on ESP32-S3.

Two cautions for whoever implements this. First, the real cost is larger than
"the neighbouring channel": 6 blocks on ESP32-S3 consume six of eight channels,
and 4 blocks on ESP32-S2 consume the entire RMT memory, so nothing else in the
firmware can use RMT. That is the trade-off to weigh, and it is why this is a
proposal rather than a change. Second, the legacy driver's internal check is a
hardcoded `<= 8`, so on ESP32-S2 it will accept a `mem_block_num` that does not
fit the 3-bit hardware field; clamp to `SOC_RMT_CHANNELS_PER_GROUP` yourself.

For the typical two-LED status use the numbers are undramatic: 49 words, one
block on ESP32-S2 and two on ESP32-S3.

Validate with a logic analyzer while writing to flash, before and after.

## 2. The backend object is heap-allocated and could reach PSRAM (Low)

`createBackend()` uses `new (std::nothrow)`, and both RMT backends keep their
transmit buffer as a member. On an ESP32-S3 build with PSRAM, Arduino-ESP32 ships
`CONFIG_SPIRAM_USE_MALLOC=y` with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, so a
larger allocation would be served from PSRAM.

The first pass of this audit rated this Medium on the grounds that the buffer is
read from an ISR. That framing was wrong and is corrected here:

- The objects are **980 bytes** (legacy, dominated by `rmt_item32_t _items[241]`)
  and **52 bytes** (RMT v2). Both are below the 4096-byte threshold, so both land
  in internal RAM today. The legacy object would only cross it at
  `kMaxLedCount >= 43`; the RMT v2 object never can, since `ledCount` is a
  `uint8_t`.
- The legacy driver's PSRAM guard is `#if CONFIG_SPIRAM_USE_MALLOC` **and**
  `intr_alloc_flags & ESP_INTR_FLAG_IRAM`. It rejects a PSRAM buffer with
  `ESP_ERR_INVALID_ARG`; it does not protect against silent corruption.
- The two hazards are mutually exclusive. With the ISR non-IRAM (today) it runs
  only while the cache is enabled, so a PSRAM buffer would be read correctly.
  A PSRAM buffer only matters once the ISR is made cache-safe, and in that case
  the driver refuses the transfer outright.
- Note 4096 is Arduino's value. Plain ESP-IDF defaults to 16384.

So there is nothing to fix for its own sake. It matters as a **prerequisite for
issue 1**: making the interrupt cache-safe requires the item buffer, the callback
and the `user_data` object to be in internal RAM.

**Proposal: drop the heap entirely when issue 1 is addressed.** Only one backend
is compiled per build, so static storage works and also satisfies the project's
no-heap rule:

```cpp
// In each backend file, replacing new/delete:
alignas(BackendIdfWs2812) static uint8_t g_storage[sizeof(BackendIdfWs2812)];
static bool g_used = false;

BackendBase* createBackend() {
  if (g_used) return nullptr;          // one instance per build
  g_used = true;
  return new (g_storage) BackendIdfWs2812();
}
void destroyBackend(BackendBase* backend) {
  if (backend) { backend->~BackendBase(); g_used = false; }
}
```

This makes `StatusLed` single-instance per process, which is already true in
practice. To keep multiple instances possible, keep `new` but allocate with
`heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`.

## 3. `T0H` exceeds the window of every part made since 2017 (Medium)

Both RMT backends encode a zero bit as 400 ns high and 850 ns low. That is the
nominal from the 2013 and 2016 WS2812B datasheets, which allow 250-550 ns. Every
WorldSemi sheet published since about 2017 narrows it, **including the current
plain WS2812B sheet**, which is the correction that matters most here: this is
not only about the V5 variant.

| Datasheet | T0H window |
| --------- | ---------- |
| WS2812 (original) | 200-500 ns |
| WS2812B (2013, Jan 2016) | 250-550 ns |
| WS2812B (current revision) | **220-380 ns** |
| WS2812B-V5, WS2812B-V5/W, WS2812C, WS2812C-2020 | **220-380 ns** |
| SK6812 | 150-450 ns |

So 400 ns is out of spec for current stock of the plain part as well as for the
V5 generation.

It works on the parts this library has run on, but the evidence for that is
weaker than the first pass of this audit implied, and the wording is corrected
here. The measurements everyone cites were taken in 2014 on the original WS2812
and the then-current WS2812B. They report that a zero was still read correctly
with a high pulse as short as 62.5 ns, which is the one-CPU-cycle floor of the
test rig rather than a measured limit, that a zero "should not be longer than
~500 ns (maximum on WS2812)", and that the decision threshold sits "somewhere
between 563 and 625 ns". The same author cautions that batches differ and the
numbers are a rough indication. None of it is evidence about V5 silicon.

**Proposal: use 325 ns, that is 13 ticks at 40 MHz.** The intersection of every
published T0H window is 250-380 ns, so 325 ns is the best-centred value on an
integer tick boundary, with 75 ns of margin below and 55 ns above. The 350 ns
(14 ticks) suggested in the first pass is also inside every window but sits only
30 ns from the top.

```cpp
static constexpr uint16_t kT0H = 13;  // 0.325 us, centred in every WS2812x window
```

Shortening `T0H` alone leaves the zero bit at 1200 ns rather than 1250 ns. That
is still inside the 1.25 us +/- 600 ns period every sheet that states one allows,
and above the 1063 ns minimum period the 2014 measurements found; to keep 1250 ns
exactly, raise `T0L` from 34 to 36 ticks (900 ns), which is inside every T0L
window.

**The other three timings are not all fine, contrary to the first pass of this
audit.** Corrected:

- `T0L = 850 ns` is inside every published window (intersection 750-950 ns).
- `T1H = 800 ns` is in spec for every WorldSemi part but **exceeds the SK6812
  maximum of 750 ns** (0.6 us +/- 0.15). The intersection across all sheets is the
  single value 750 ns, so drop `T1H` to 30 ticks if SK6812 drop-in support is
  wanted; leave it at 32 if only WorldSemi parts are targeted.
- `T1L = 450 ns` **cannot be made universally compliant**. The current WS2812B,
  WS2812C and WS2812C-2020 V1.0 sheets cap it at 420 ns, while WS2812 and SK6812
  require at least 450 ns, so the intersection is empty. The design instead relies
  on the 1.25 us bit period, which every sheet that states a period accepts.

On the suspicious `T1L` rows: WS2812B-V5, WS2812B-V5/W and WS2812C-2020 V1.2 give
`T1L` as 580 ns to 1 us, identical to their own `T1H` and `T0L` rows and
inconsistent with a 1.25 us bit period. The strongest evidence that this is an
editing error is that the same part changed: WS2812C-2020 V1.0 (20180701) says
220-420 ns and V1.2 (20190104) says 580 ns to 1 us, with no other timing change.
The first pass called it a copy of the `T0L` row and said the timing diagrams
contradict it; both are overstated. The sequence chart is an unlabelled waveform
that merely draws `T1L` shorter than `T0L`, and three rows are identical, so the
mechanism is a guess. Treat the wide row as unresolved rather than proven wrong.

Validate with a logic analyzer on an old WS2812B and a WS2812B-V5 before
releasing any of these changes.

## 4. Output errors are erased by the next successful call (Medium)

`tick()` records a backend failure by writing `_lastStatus` directly, but every
fallible public method routes through `setLast()`, which overwrites the field on
success as well. A consumer that polls `getLastStatus()` on its own schedule,
which is what a supervising firmware does, will usually see `OK` no matter how
often transmission fails.

The first pass added that there is "no way to distinguish the driver rejecting a
frame from the caller passing a bad index". That is overstated and is corrected
here: the codes already differ, `HARDWARE_FAULT` against `INVALID_CONFIG`. What is
genuinely missing is provenance, which call last wrote the field, and durability,
since a transient failure is simply lost.

**Proposal: keep `lastStatus()` as the call-result channel and add a separate
sticky output-health pair.** It is cheap and changes no existing semantics:

```cpp
/// @brief Frames the backend rejected since begin(), excluding RESOURCE_BUSY.
uint32_t outputErrorCount() const { return _outputErrors; }
/// @brief Status of the most recent failed transmission (cleared by begin()).
Status lastOutputStatus() const { return _lastOutputStatus; }
```

with the two members `uint32_t _outputErrors = 0;` and
`Status _lastOutputStatus{};`, which the first pass omitted from the snippet.
Set both inside the existing non-busy error branch in `tick()`, which already
excludes `RESOURCE_BUSY`, and reset them in `begin()`.

## 5. CI does not build the RMT v2 environments (Medium)

The workflow builds `cli_esp32s3_idf`, `cli_esp32s2_idf` and the two NeoPixelBus
environments, plus a native test job. It does not build `cli_esp32s3_idf5` or
`cli_esp32s2_idf5`, which are the environments the recommended backend uses, and
nothing builds the ESP-IDF component. The RMT v2 backend is compiled by hand only.

**Proposal:** add both IDF5 environments to the existing matrix, and add a job
that builds the component the way a consumer does:

```yaml
  build-espidf:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.3
          target: esp32s3
          path: examples/espidf_basic
```

Verified: the action exists, `esp_idf_version`, `target` and `path` are its real
input names, and `v5.3` matches the `idf: ">=5.3"` floor in `idf_component.yml`.
The example's `EXTRA_COMPONENT_DIRS "../.."` resolves correctly because the action
mounts the checkout at a path ending in the repository name, and ESP-IDF registers
a directory containing a `CMakeLists.txt` as a single component named after that
directory. One consequence worth knowing: the component name comes from the
directory, so **renaming the repository breaks this job**, the same constraint the
README already states for consumers.

This also replaces what the deleted `check_idf_example_contract.py` approximated
with text matching: a real compile.

## 6. `kMaxLedCount` is fixed at 10 (Low)

The limit is baked into the public class and propagates to `_leds`, `_frame`,
`indexValid()`, the `begin()` validation, the legacy backend's `kMaxItems` and the
RMT v2 backend's `kMaxPayloadBytes`. It suits status indication, which is the
library's purpose, but a consumer needing 12 LEDs must patch the header.

**Proposal:** make it overridable while keeping the default. The buffer sizes do
follow automatically, since both backends derive theirs from `kMaxLedCount`
through `constexpr` expressions.

```cpp
#ifndef STATUSLED_MAX_LED_COUNT
#define STATUSLED_MAX_LED_COUNT 10
#endif
static_assert(STATUSLED_MAX_LED_COUNT >= 1 && STATUSLED_MAX_LED_COUNT <= 255,
              "STATUSLED_MAX_LED_COUNT must be 1..255");
static constexpr uint8_t kMaxLedCount = STATUSLED_MAX_LED_COUNT;
```

Three constraints the first pass did not state:

- The `static_assert` is not optional. Plain copy-initialization into a `uint8_t`
  silently truncates, so 300 would quietly become 44.
- The macro sizes two member arrays, so `sizeof(StatusLed)` depends on it. It must
  be set as a build-system flag (PlatformIO `build_flags`, or
  `target_compile_definitions(... PUBLIC)` for the component). Defining it in one
  translation unit before the include makes translation units disagree on the
  class layout, which is silent undefined behaviour.
- There is no RMT hardware ceiling, because the item array is host RAM streamed by
  the refill interrupt. The real ceilings are RAM, roughly 24 KB for the legacy
  item array at 255 LEDs, and the `uint8_t` of `Config::ledCount` and every index
  parameter, which caps the value at 255.

Raising the count also widens the exposure window in issue 1.

Prose that hardcodes 10 and would need updating: the `Config::ledCount` Doxygen
range, and two places in the README.

## 7. A persistently failing backend is retried every tick (Low)

`_frameDirty` is cleared only on a successful `show()`, so both the busy path and
the hardware-fault path leave it set and the next `tick()` retries immediately,
forever. That is correct for the transient `RESOURCE_BUSY` case and wasteful when
the driver is genuinely broken.

The first pass said this "burns the full driver call on every loop iteration".
Qualified: it does so only for a driver that is installed but failing. If the
fault leaves the backend uninstalled, `canShow()` returns false and `show()` is
never reached, which costs nothing. For the legacy backend the wasted work is
substantial, since `canShow()` is itself a driver call and `show()` re-encodes all
241 items before failing.

**Proposal:** on a non-busy error, hold off retrying for a fixed interval, say
100 ms, using the `now_ms` already passed to `tick()` and the existing
wraparound-safe `timeReached()` helper rather than a raw comparison. Two members
and three lines, no new API.

## 8. The two Arduino platforms contend for one framework package (Low, tooling)

The `*_idf` environments build on `espressif32@6.12.0` (Arduino core 2.0.17) and
the `*_idf5` environments on the pioarduino platform (Arduino core 3.x). Both
platforms declare a package named `framework-arduinoespressif32`, at different
versions, and both resolve it into the same shared package directory.

Building one family therefore takes over that directory. The first pass said this
"evicts" the other framework and forces "a full framework download each time",
which is wrong and is corrected here: PlatformIO **detaches** the displaced
package, copying it to a sibling directory suffixed `@<version>` for a registry
spec or `@src-<md5-of-url>` for a URL spec, and finds it again by reading package
metadata rather than by directory name. Nothing is lost and nothing is
re-downloaded. What each switch does cost is a full recursive copy of one
multi-hundred-megabyte tree and an overwrite of another, which on Windows is
exactly where the partially-extracted package, the missing `Arduino.h` and
`HWCDC.cpp`, and the version-mismatch failure inside PlatformIO's own
`arduino.py` came from while validating this audit. It is the same long-path
fragility the README already warns about. No library code was involved.

There are currently three contenders for that one directory, not two: the
directory holds core 3.3.11, which belongs to a newer pioarduino release than the
54.03.20 pinned in `platformio.ini`, so the `*_idf5` family alone will re-resolve
on its next build.

**Proposal: give each platform family its own core directory.** The first pass
showed this as a commented `[env:cli_esp32s3_idf5]` snippet, which is inert and
is withdrawn: `core_dir` is a `[platformio]` section option and there is exactly
one `[platformio]` section per config file, so it cannot vary per environment.
The mechanisms that actually work, in order of practicality:

1. Set `PLATFORMIO_CORE_DIR` in the environment before invoking a build. The
   README already documents this variable for the long-path problem, so this is
   an extension of an existing note rather than a new mechanism.
2. Keep a second config file with its own `[platformio] core_dir` and select it
   with `pio run -c platformio-idf5.ini -e cli_esp32s3_idf5`. This is the only
   file-based way to make it per-family.
3. Drop the Arduino core 2.x environments once the legacy RMT backend is no
   longer needed. That also removes the NeoPixelBus backend's only supported
   configuration.

Teaching `scripts/pio.cmd` to set the variable when the environment ends in
`_idf5` is feasible and `setlocal` already scopes it, but it is a partial
mitigation, not a fix: it must replicate `default_envs` when no `-e` is given, it
has no correct answer when one command names environments from both families, and
it does not cover CI or the VS Code extension, neither of which goes through the
wrapper. Separate core directories also duplicate the toolchains, several GB, and
lengthen paths, pulling against the very long-path constraint being worked around.

---

## Verified correct

Recorded so a later reader knows what was covered, and re-checked in the second
pass. Two claims from the first pass were wrong and are corrected below.

- **Wraparound.** The half-range `timeReached()` comparison, the first-tick clock
  adoption, the `kMaxDurationMs` bound that keeps `now + duration` from landing in
  the past, and the modular remaining-time subtraction are correct across the
  49.7-day wrap. The `nextDeadline()` helper is wraparound-safe too: all its
  arithmetic is `uint32_t`, so a deadline crossing 2^32 is exactly the modular
  deadline, and `durationMs` is a `uint16_t`, far below the half-range that would
  make the comparison ambiguous.
- **Bounds.** `indexValid()` checks both the configured count and the array size;
  the per-LED loops clamp through `safeLedCount()`, except the `begin()` reset
  loop which runs to the array size itself; the legacy RMT item builder
  bound-checks before every write; the RMT v2 payload writer is bounded by a
  validated `count`. The pattern-table index is safe because every write to
  `LedState::mode` is immediately followed by `startMode()`, which resets `phase`
  to 0, so switching from the 18-step SOS table to a 4-step table cannot read out
  of bounds.
- **Arithmetic.** `scale8()` cannot overflow (its largest intermediate is 65152),
  `ease8InOut()` stays in range and reaches both endpoints, `lerpU8()` handles
  descending ramps and clamps, and the LFSR has a zero-lockup guard with a correct
  16-bit tap mask. In the pulse path `period - half` is at least 1 because
  `periodMs` is forced to at least 2, and `phase - half` is always strictly less
  than `period - half`, so odd periods and the minimum period are both safe; the
  output reaches exactly `minLevel` and `maxLevel`.
- **Retransmit policy.** The frame comparison in `refreshLedOutput()` means static
  modes never retransmit; `RESOURCE_BUSY` keeps the frame dirty and retries;
  `forceRefresh()` works.
- **Compile-time backend selection.** The macro checks reject zero or multiple
  backends, so no build can link two RMT driver families.
- **Wire format.** GRB, MSB first, 24 bits per pixel is correct for every
  WorldSemi WS2812x part and for the 24-bit SK6812, and both RMT backends feed the
  engine's logical-RGB frame through the same `writePixelBytes()`. The NeoPixelBus
  path is equivalent: with `NeoGrbFeature` the wire order is G,R,B, so passing the
  logical color for a GRB strip and a red/green swap for an RGB strip produces the
  right bytes.
  **Corrected from the first pass, which claimed all four bit timings are in spec
  for all of those parts.** They are not: `T0L = 850 ns` is universally in spec,
  but `T1H = 800 ns` exceeds the SK6812 maximum of 750 ns, and `T1L = 450 ns` lies
  outside the window published by the 2017-and-later WorldSemi sheets. See issue 3.
- **RMT v2 usage.** Payload lifetime is held across the transaction by the busy
  flag; `eot_level = 0` holds the line low after the frame and after
  `rmt_disable()`, since the driver writes the idle level into the channel before
  starting and never clears it; the `on_trans_done` callback only does an atomic
  store, which is lock-free and ISR-safe on both architectures; 40 MHz divides
  exactly from the 80 MHz APB clock, so no resolution-loss warning is emitted. The
  composite encoder matches the official `led_strip` encoder, including re-entry
  after `RMT_ENCODING_MEM_FULL`, and pairs `rmt_alloc_encoder_mem()` with `free()`
  as that example does.
  **Corrected from the first pass**, which said `trans_queue_depth = 1` never
  blocks "because `show()` checks the busy flag first". The flag check alone would
  not be sufficient. The guarantee holds because the driver pushes the finished
  descriptor onto its completion queue **before** invoking `on_trans_done`, so by
  the time the flag clears a descriptor is already available and `rmt_transmit()`
  returns immediately. That ordering is identical in 5.3 and 6.0. If it ever
  inverted, this backend would need `queue_nonblocking`.
- **Legacy RMT usage.** The item array must be a member, because
  `rmt_write_items(..., false)` transmits directly out of the caller's buffer;
  `rmt_wait_tx_done(ch, 0)` is the documented poll and does not log; channels 0-3
  are TX-capable on both ESP32-S2 and ESP32-S3; leaving `flags = 0` keeps the
  channel on the 80 MHz APB clock rather than the DFS-aware source, which would be
  XTAL on ESP32-S3 and 1 MHz REF_TICK on ESP32-S2. The 300 us reset item splits
  6000 ticks across both halves, inside the 15-bit duration field, and the driver
  appends its own zero-duration terminator after it, so nothing is truncated.
- **ESP-IDF 6.x compatibility.** Every RMT TX function this library calls is
  unchanged in signature from 5.3 to 6.0. The removed `io_od_mode` and
  `io_loop_back` fields are not used.

---

## Resolved

Fixed during this audit and folded into the documentation. Listed so an
independent reviewer can check the implementation rather than rediscover the
problem. Full descriptions are in `CHANGELOG.md` under 1.4.0.

| Was | Resolution | Where to check |
| --- | ---------- | -------------- |
| No WS2812 latch gap in the RMT v2 backend; 80 us in the legacy one | Composite encoder appending a 300 us reset; legacy reset raised to 300 us | `src/StatusLedBackendIdf5.cpp`, `src/StatusLedBackendIdf.cpp`, `kResetUs` in `src/StatusLedInternal.h` |
| `mem_block_symbols = 64` stole a second TX channel on ESP32-S3 | Uses `SOC_RMT_MEM_WORDS_PER_CHANNEL` | `src/StatusLedBackendIdf5.cpp` |
| Data line left floating high after `end()` | Both backends detach the signal and drive the pin low, including after a failed `begin()` | `end()` in both RMT backends |
| Pattern corruption every 256 steps; blink and pulse defects; temporary-preset lifecycle defects | Engine rewrite | `src/StatusLed.cpp`, 15 regression tests in `test/test_status_engine.cpp` |
| NeoPixelBus backend failed deep inside the dependency on Arduino core 3.x | `#error` guard inside the backend's own `#if` block, so other builds are unaffected | `src/StatusLedBackendNeoPixelBus.cpp` |
| `ColorOrder` did not say what it excludes | Doxygen note on the enum: 24-bit parts only, no RGBW or BRG | `include/StatusLed/Config.h` |
| 3.3 V drive into 5 V parts undocumented | Hardware notes section | `README.md` |
