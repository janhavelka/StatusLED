# Code audit verification and changes

Reviewed on **2026-09-05**, starting from **a7e0e4e** on
`feature/statusled-idf-port`, after synchronizing with its upstream.
This review covers all eight findings and every entry marked "Verified correct"
or "Resolved" in [CODE_AUDIT.md](CODE_AUDIT.md). That document is retained as the
original assessment; the verdicts below describe the resulting implementation.

The independent follow-up was checked against **fc92d52** on **2026-09-06**.
Its evidence gaps and documentation drift were confirmed and corrected without
changing engine behavior. The existing capacity builds did use different array
sizes; the gap was that symbolic assertions could not detect a missing override.
`native_max` now has an independent test marker requiring capacity 255, while
`native` requires 10. All 60 test registrations are retained in each environment.

Release follow-up on **2026-09-07** set the additive public API release to
**1.5.0**, regenerated the tracked version header and aligned the package,
component, Doxygen and README versions. The historical audit's framing now
explicitly scopes its findings and 43-test baseline to `a7e0e4e` while preserving
the original finding table and body. The CI evidence below covers release
commit **ea30d48**, including the follow-up regression and workflow assertions.

## Findings and decisions

| Finding | Verdict and action | Why this solution; remaining limits |
| --- | --- | --- |
| **1. Flash writes interrupt RMT refill** | **Valid.** Added optional `Config::rmtFullFrameBuffer` to both RMT backends, reserving data, reset and EOF before transmission. Added IRAM placement for the IDF5 encoder/reset/completion callbacks. | Full buffering removes the refill deadline without requiring changes to precompiled Arduino SDKs. Making it optional preserves existing RMT resource sharing. **Default one-block streaming remains susceptible** when cache-safe interrupts are disabled. |
| **2. Backend allocation can reach PSRAM** | **Valid risk; report's reassurance and singleton proposal rejected.** Both RMT backend objects now use one explicit internal-capability allocation, placement construction, and matching destruction/free. | Allocation thresholds express a preference, not a guarantee: allocation can fall back to PSRAM. Static singleton storage would unnecessarily prohibit multiple instances. Initialization-time allocation is already permitted; steady-state operation remains allocation-free. See [ESP-IDF allocator fallback](https://github.com/espressif/esp-idf/blob/v5.3/components/heap/heap_caps.c#L107). |
| **3. Zero-high timing outside newer specifications** | **Valid for documented revisions; universal dating claim unproven.** Shared RMT constants now generate 325 ns high and 925 ns low, retaining exactly 1250 ns per bit. | This corrects a demonstrated zero-bit mismatch with one shared definition. The proposed tick arithmetic was wrong. One-bit timings remain 800/450 ns pending qualification against the actual LED revision; universal compatibility is not claimed. |
| **4. Output errors overwritten by later success** | **Valid.** Added `outputErrorCount()` and `lastOutputStatus()`, retaining existing last-call status semantics. | A separate durable health channel is additive and keeps normal call results useful. Counter saturates, excludes `RESOURCE_BUSY`, and survives successful calls, successful output and `end()`. A new initialization attempt that passes common configuration validation clears it; rejected common configuration does not. Const getters never erased errors. |
| **5. Missing RMT v2/component CI** | **Valid.** Added both Arduino RMT v2 targets, native tests at default/255 capacity, and native ESP-IDF 5.3/6.0 builds on S2/S3. S3 variants enable the appropriate cache-safe option. Feature pushes now run CI. | Real consumer builds exercise framework boundaries better than textual contract checks. Versions cover the declared minimum and documented 6.x support. Action inputs and checkout path were verified against the [official action](https://github.com/espressif/esp-idf-ci-action/blob/v1/action.yml). CI configuration is not evidence of completed execution. |
| **6. Fixed maximum of ten LEDs** | **Valid limitation, not a correctness defect.** Added build-wide `STATUSLED_MAX_LED_COUNT`, default 10, with a compile-time 1..255 guard. | Existing users retain their capacity. All translation units must use the same value because it changes class layout; public build definitions are documented. Tests exercise the last valid index and bulk operations at 255. Full-frame hardware limits remain separate from the software capacity. |
| **7. Persistent failures retried each tick** | **Valid.** Non-busy output failures defer readiness polling and retransmission for 100 ms using the existing wraparound-safe clock. | A fixed deadline needs no timer/task or new configuration. Animations continue and updates coalesce during backoff. Ordinary busy responses retry normally without becoming output errors; `forceRefresh()` respects the failure deadline. |
| **8. Shared framework package contention** | **Partly valid; mechanism corrected.** Kept pinned platforms and the existing wrapper. Documented optional separate short `PLATFORMIO_CORE_DIR` storage paths for simultaneous incompatible framework installation/builds. | Installed versions can coexist: metadata lookup returns matching packages before installation/copying. New registry installations may detach packages; custom-named URL installations may overwrite the unsuffixed directory. Neither "every switch copies" nor "nothing is ever displaced" is correct. Automatic environment-name parsing in the wrapper would add fragile policy. See [lookup](https://github.com/platformio/platformio-core/blob/v6.1.19/platformio/package/manager/base.py) and [installation branches](https://github.com/platformio/platformio-core/blob/v6.1.19/platformio/package/manager/_install.py). |

For finding 1, the allocation is `ceil((24 * ledCount + 2) / wordsPerBlock)`.
Ten LEDs require six 48-word blocks on S3 or four 64-word blocks on S2.
Full buffering can hold at most 15 LEDs on S3 or 10 on S2, subject to placement,
availability and the configured software capacity. The IDF5 allocator reserves
borrowed blocks; the legacy backend checks installed channels' occupied ranges
before configuration. Applications must serialize legacy RMT resource
initialization and teardown and coordinate its shared interrupt policy. Internal
buffer placement alone does not make a legacy driver cache-safe. Oversized requests and occupied resources return
explicit errors. The [legacy driver](https://github.com/espressif/esp-idf/blob/v4.4.8/components/driver/rmt.c)
and [IDF5 TX driver](https://github.com/espressif/esp-idf/blob/v5.3/components/esp_driver_rmt/src/rmt_tx.c)
confirm the allocation/refill distinction.

Cache-safe streaming additionally requires the SDK option:
`CONFIG_RMT_ISR_IRAM_SAFE` on 5.3/5.4 or
`CONFIG_RMT_TX_ISR_CACHE_SAFE` on newer SDKs. The IDF6 reset wrapper is placed
in IRAM by its [linker rules](https://github.com/espressif/esp-idf/blob/v6.0/components/esp_driver_rmt/linker.lf);
on IDF 5.3/5.4 the SDK reset helpers run only during task-context disable and
may remain in flash. The library's internal payload/context allocation and IRAM callbacks complete
its side of that contract. This does not enable cache-safe interrupts in an
already compiled Arduino SDK.

For finding 3, the [older WS2812B sheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf)
allows 250..550 ns T0H, while a [later WS2812B sheet](https://docs.electrokit.com/modules/EKM019/WS2812B.pdf)
specifies 220..380 ns. Both accept 325 ns T0H and 925 ns T0L.
At 25 ns/tick, the report's 13+34 ticks equal **1175 ns**, and 13+36 equal
**1225 ns**; the implementation uses **13+37 = 1250 ns**.

The report also overstates the evidence for a T1L datasheet error: two
580..1000 ns windows can total 1250 ns, for example 650+600 ns. Conflicting
revision requirements remain unresolved. Its SK6812 statement is
revision-specific: 800 ns exceeds the maximum in
[SK6812 Rev. 01](https://cdn-shop.adafruit.com/product-files/1138/SK6812%20LED%20datasheet%20.pdf),
but fits the later
[SK6812-012 specification](https://www.ledyilighting.com/wp-content/uploads/2025/02/SK6812-datasheet.pdf).
Manufacturer datasheets are linked through their available distributor mirrors;
none establishes a manufacturing-date rule for every part.

For finding 8, concurrent external VS Code project initialization was observed
replacing the shared Arduino framework during validation; an active build then
lost `default.csv`. This supports installation/build interference, not a claim
about every sequential environment switch. Validation therefore uses separate
short storage directories with the **same VS Code-managed Core executable**.
The [documented storage override](https://docs.platformio.org/en/latest/projectconf/sections/platformio/options/directory/core_dir.html)
does not require installing another Core.

## Recheck of every "Verified correct" entry

| Original entry | Verification and resulting qualification |
| --- | --- |
| **Wraparound** | Half-range deadlines, first-tick clock adoption, bounded temporary duration, modular remaining time and `nextDeadline()` are correct with periodic ticks. The broader assurance missed pulse phase loss after a full 2^32 ms of operation: pulse origins now advance each cycle. Completed fades remain completed through wrap and overlays. |
| **Bounds** | Configuration/index guards, bounded LED loops, per-item checks and validated payload lengths remain sound. Pattern phases stay within their mode's table, including restored overlays. Capacity and last-index regressions now run at 10 and 255. |
| **Arithmetic** | Confirmed scaling's maximum intermediate of 65152, easing bounds/endpoints, signed descending interpolation and guarded LFSR state. Pulse periods of two and odd periods retain valid denominators and reach both configured bounds; dedicated tests cover these cases. |
| **Retransmit policy** | Confirmed static-frame suppression, quantized fade suppression, dirty-frame preservation/coalescing and forced refresh. Null-backend output counters now test actual submissions and readiness polling. Failed output intentionally adds the documented retry interval. |
| **Compile-time backend selection** | Header guards reject zero/multiple selected backends; the component compiles only the engine and IDF5 backend and exports its definitions. This prevents this library from selecting both driver families; consumers must also avoid introducing the other family through unrelated dependencies. |
| **Wire format** | Confirmed 24-bit, MSB-first serialization for supported GRB/RGB orders, and the equivalent NeoPixelBus red/green swap with `NeoGrbFeature`. Added byte-order/guard-byte checks. Format correctness does not establish electrical compatibility with every WS2812x or SK6812 revision; see finding 3. |
| **RMT v2 usage** | Confirmed payload lifetime, low idle output, 40 MHz clock resolution, composite encoder re-entry and matching encoder allocation/free. `queue_nonblocking` now explicitly prevents queue waits. The busy flag uses only atomic acquire-load/release-store; exact Espressif GCC 14.2 probes on S2/S3 emitted byte accesses and memory barriers without calls or locks. S2's broader atomic lock-free macro also covers unused read-modify-write operations and cannot validate this narrower requirement. |
| **Legacy RMT usage** | Confirmed persistent item storage, zero-time readiness polling, TX channels 0..3, APB clock selection, 300 us reset and driver EOF placement. Each reset half is 6000 ticks, within its 15-bit field. The old shutdown's blocking `rmt_write_items(..., true)` could wait indefinitely: replaced with asynchronous submission, bounded waits, stop and uninstall. |
| **ESP-IDF 6.x compatibility** | Called API signatures remain compatible, but that did not prove behavior unchanged. IDF6 [channel destruction](https://github.com/espressif/esp-idf/blob/v6.0/components/esp_driver_rmt/src/rmt_tx.c#L196) no longer resets the GPIO matrix. Cleanup now explicitly detaches it before driving low, preventing later channel reuse from driving the released pin. Native 6.0 CI was added. |

## Recheck of every "Resolved" entry

| Previously resolved item | Result |
| --- | --- |
| **Missing/short latch gap** | Confirmed both RMT backends append a 300 us reset symbol. The final data bit's low half extends the continuous low interval beyond 300 us; the SDK EOF marker is not used as a substitute for the timed reset. Wire-level validation remains pending. |
| **64 symbols consuming two S3 blocks** | Confirmed default allocation is exactly one hardware block, 48 symbols on S3. Additional blocks are now requested only through explicit full-frame configuration. |
| **GPIO cleanup, including failed begin** | **Not fully resolved in the baseline.** Legacy failed initialization destroyed an object without calling `end()`. Backend destructors now invoke idempotent cleanup; partial legacy installation is released. Added explicit IDF6 matrix detachment as described above. |
| **Pattern/blink/pulse/temporary lifecycle rewrite** | Original regressions remain covered. Additional defects were found and fixed: overlays now preserve blink/pattern phase, remaining step time and alternate color; completed fades remain complete; invalid preset values leave overlays intact; long-running pulses preserve phase. |
| **NeoPixelBus/core 3 guard** | Confirmed the rejection remains inside the selected backend block, protecting other builds. A clean installation also exposed an unresolvable registry dependency: both environments now pin the identical 2.7.6 source to [its upstream commit](https://github.com/Makuna/NeoPixelBus/commit/0ce9acd6f71312deff36a06f33319a9f8762bbc8). |
| **ColorOrder exclusions** | Confirmed public documentation limits the interface to three-byte GRB/RGB output and excludes RGBW/BRG. No fourth-channel support was implied or added. |
| **3.3 V drive into 5 V parts** | Hardware guidance remains necessary: the [older WS2812B specification](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf) requires VIH >= 0.7 VDD, or 3.5 V at 5 V. README recommends suitable level shifting and part-specific qualification. Later thresholds cannot be assumed for unidentified LEDs. |

Both CLIs additionally share strict unsigned decimal parsing, including GPIO
arguments, and reject signed, overflowing, trailing-text and invalid color-order
input. This removes duplicated parsing behavior. They expose identical full-frame
configuration and persistent-health diagnostics. After a rejected `begin`, CLI
state now mirrors the actual initialized state and accepted configuration instead
of incorrectly marking a preserved running instance stopped.

## Validation and remaining qualification

| Check | Result |
| --- | --- |
| Baseline host tests | 43 passed. |
| Final host tests | 60 passed in `native`; 60 passed in `native_max` at capacity 255: **120 executions passed**. |
| Final six Arduino firmware environments, isolated framework storage | All passed: `cli_esp32s3_idf`, `cli_esp32s2_idf`, `cli_esp32s3_idf5`, `cli_esp32s2_idf5`, `cli_esp32s3_neopixelbus`, `cli_esp32s2_neopixelbus`. |
| Capacity/backend compile guards | Capacity 1/10/255 accepted; 0/256/300 rejected. Zero/multiple backend selections rejected. |
| CLI parity | 26 commands and 33 distinct mode/preset names match. |
| Final S2/S3 IDF5 object inspection | Encoder/reset/completion functions are in IRAM sections; completion callback contains byte stores and barriers with no calls or locks. |
| Documentation/source checks | Doxygen generation, text-integrity and `git diff --check` passed. |
| Local native ESP-IDF 5.3.1 S3, cache-safe SDK | Passed using the native example sources and repository component through PlatformIO 6.9.0. Generated SDK config confirms `CONFIG_RMT_ISR_IRAM_SAFE=y`; linked library callbacks, SDK byte/copy encoders and TX ISR are in IRAM. |
| Native ESP-IDF 5.3/6.0, S2/S3 CI | All four jobs passed at `ea30d48` in [CI run 34134767443](https://github.com/janhavelka/StatusLED/actions/runs/34134767443) on 2026-09-07, alongside six Arduino builds and both native suites (60/60 each): **all 12 jobs passed**. Both S3 jobs passed the new generated-`sdkconfig` assertions. |
| Hardware boot/basic LED smoke tests | Not performed; deferred. Hardware testing is optional under the engineering guidelines updated on 2026-09-06 and does not block commits or pushes. |
| Waveforms, flash-write overlap, GPIO release and representative LED revisions | Not performed. |

Follow-up coverage extends the existing tests with real output failures at
`UINT32_MAX - 1` and `UINT32_MAX`, using a host-only friend to seed just the counter.
Saturation, continued error-detail updates, busy exclusion and successful-output
persistence are asserted through `tick()`. Both preset setters are checked with
an active overlay and again with a valid replacement queued; the replacement's
preset, duration and restoration are verified after rejection. Null-backend
initialization failure injection verifies that both history fields are cleared,
the initialization error is returned and a later initialization can recover.

The strengthened suites pass **60/60 in each environment**. Seven mutations in
an isolated copy were rejected: removing, misspelling or changing the capacity
flag; replacing saturation with an unconditional increment; validating a temporary
preset after changing state; and independently moving each history reset after
successful backend initialization. These claims now have regression evidence,
rather than inspection alone.

CI now appends the cache-safe setting without overwriting existing SDK defaults
and checks the exact enabled option in the generated `sdkconfig`. Local command
probes check preservation of existing defaults, including a missing final newline,
and rejection of disabled, unknown or absent options and a missing generated file.
Both assertions passed in [CI run 34134767443](https://github.com/janhavelka/StatusLED/actions/runs/34134767443):
the post-build workspace contained the generated `sdkconfig` with
`CONFIG_RMT_ISR_IRAM_SAFE=y` on 5.3/S3 and
`CONFIG_RMT_TX_ISR_CACHE_SAFE=y` on 6.0/S3. No further workflow correction was
needed.

The new tests cover fault persistence, retry deadlines/wraparound, busy coalescing,
static output, capacity boundaries, overlay restoration, long-lived pulses,
parsing and RMT symbol/byte contracts. Hardware qualification must establish actual
pulse widths, reset gaps and behavior during flash operations. Successful builds
and host tests do not establish those electrical results.
