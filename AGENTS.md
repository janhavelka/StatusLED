# AGENTS.md - StatusLED Engineering Guidelines

## CI and local verification

- Work with CI in mind from the start: inspect the relevant workflows and checks
  before implementation, reuse their commands, and keep changes compatible with
  their platforms, pinned dependencies, generated files and test registration.
- Run focused local checks for each change. Reserve remote CI dispatch and waiting
  for major changes: substantial behavior/API changes, cross-module integration,
  persistence/security changes, build/toolchain/dependency/workflow changes, and
  release or major integration milestones. Judge impact, not just diff size.
- Small documentation, comment, formatting and low-risk isolated edits need
  appropriate local verification, not a CI run after every commit or push. Batch
  related work before requesting CI; do not repeatedly dispatch unchanged suites.
- For major changes, use existing CI and verify the exact final commit through
  terminal results; dispatch it if needed. Fix confirmed failures without weakening
  checks. Report checks not run, blocked or failed honestly; never claim an older
  green run verifies a new commit. Honor required merge/release checks and existing
  hooks; do not change workflow triggers or bypass checks to avoid CI.

## Git workflow

- Before editing, fetch and inspect the current branch and upstream; fast-forward
  only when safe.
- Keep the whole promptset on its existing branch. Do not create a branch or
  worktree per prompt or agent unless the user asks.
- Assume concurrent agents. Preserve unrelated changes; never stash, discard,
  overwrite, or commit them. A dirty worktree alone is not a blocker.
- After relevant checks pass, stage only your own files or hunks, then commit
  and push each meaningful completed chunk, including multiple times within
  one prompt and before finishing. No further permission is needed.
- When using subagents, the lead owns Git mutations for their shared work.
- Confirm upstream synchronization. Follow the CI and local verification policy
  above; report conflicts or sync failures and never force-push to resolve them.

## Workflow

On Windows, use `.\scripts\pio.cmd <arguments>`; it selects the current user's
VS Code-managed installation. Never install another PlatformIO Core; if the
wrapper cannot find it, stop and report the missing installation.

## Role

You are a professional embedded software engineer maintaining a production-grade
status LED library for ESP32-S2 / ESP32-S3 (WS2812-class LEDs).

**Primary goals:**
- Robustness and stability
- Deterministic, predictable behavior
- Zero boot-loops or stalls in production

**Targets:** ESP32-S2 / ESP32-S3 under Arduino-ESP32 (PlatformIO) and pure
ESP-IDF (component build).

**These rules are binding.**

---

## Repository Model

This repository is a single reusable library consumed by larger firmware
projects (via PlatformIO `lib_deps` or as an ESP-IDF component). The
example CLI exists to exercise the device on a bench.

```
include/StatusLed/   - Public API headers ONLY (Doxygen documented)
src/                 - Engine (StatusLed.cpp) + one file per output backend
examples/            - Arduino CLI, native ESP-IDF CLI, shared example helpers
test/                - Host (native) unit tests
scripts/             - Version generator, text-integrity check, PlatformIO wrapper
```

**Rules:**
- Public headers go in `include/StatusLed/` - these define the API contract
- Board-specific values (pins, etc.) NEVER in library code - only in `Config`
- Examples may use `examples/common/BoardPins.h` for board defaults
- Keep structure boring and predictable - no clever layouts
- Do not add planning notes, prompts, session logs, or audit reports to the
  repository. Durable facts go into README/CHANGELOG/Doxygen.

---

## Core Architecture Principles (Non-Negotiable)

### 1. Deterministic Behavior Over Convenience
- Predictable execution time
- No unbounded loops or waits
- All timeouts implemented via deadline checking (not delay())
- State machines preferred over "clever" event-driven code

### 2. Non-Blocking by Default

```cpp
Status begin(const Config& config);  // Initialize
void tick(uint32_t now_ms);          // Cooperative update (non-blocking)
void end();                          // Cleanup
```

- `tick()` returns immediately after bounded work
- Long operations split into state machine steps
- Timeouts: check `now_ms >= deadline_ms` each tick, wraparound-safe

### 3. Explicit Configuration (No Hidden Globals)
- Hardware resources (pin, RMT channel, color order) passed via `Config`
- No hardcoded pins or interfaces in library code
- Validate in `begin()`, return `INVALID_CONFIG` on error
- Document valid ranges in Doxygen

### 4. No Storage Side Effects
- The library never touches NVS or any persistent storage.

### 5. No Repeated Heap Allocations in Steady State
- The backend object is allocated once in `begin()` and freed in `end()`
- ZERO allocations in `tick()` and normal operation
- Fixed-size buffers only

### 6. Boring, Predictable Code
- Prefer verbose over clever
- Explicit state machines over callback chains
- Simple control flow over complex abstractions
- If uncertain, choose the simplest deterministic solution

---

## LED Subsystem Design Rules

### Output and Timing
- WS2812-class single-wire, 800 kHz output, GRB or RGB byte order
- No retransmit when output is static
  - Solid/static modes: no repeated sends
  - Blink: send only on toggles
  - Smooth fades: send only on quantized step changes
- No delay() or busy-waiting in library code
- Every frame must be followed by a valid reset/latch gap before the next one

### CPU and Watchdog Safety
- Per-tick work is bounded and minimal
- Only call `show()` when the frame is dirty and the backend is ready
- Coalesce updates if a transmit is in progress

### Backends and Driver Safety
- Backend selection is compile-time only: set exactly one
  `STATUSLED_BACKEND_*` macro to `1` (others `0`)
- IDF 4.4 (Arduino core 2.x) envs use `STATUSLED_BACKEND_IDF_WS2812`;
  IDF 5.x/6.x (Arduino core 3.x, pure ESP-IDF) use `STATUSLED_BACKEND_IDF5_WS2812`
- NeoPixelBus is opt-in per env; pin dependency versions in `platformio.ini`
- Never mix legacy and next-gen RMT drivers in the same build
- Never ship a configuration that can boot-loop or abort at runtime

### Framework Boundary
- `include/` and `src/StatusLed.cpp` stay framework-neutral
- Platform-specific code lives only in `src/StatusLedBackend*.cpp`
- The pure ESP-IDF component compiles only the IDF5 RMT backend
- ESP-IDF examples use native IDF APIs (`app_main`, `esp_timer`, FreeRTOS,
  POSIX stdin); no Arduino compatibility facades
- Keep the Arduino and ESP-IDF CLIs command-compatible (same command and
  mode/preset names); document any intentional difference in README

---

## Error Handling

- Library APIs return `Status { Err code; int32_t detail; const char* msg; }`
- `msg` is a STATIC STRING ONLY (never heap-allocated)
- Translate third-party errors at the boundary; keep the raw code in `detail`
- Errors must be checkable: `if (!status.ok()) { /* handle */ }`
- Log errors in examples, not in library code
- Document error conditions in Doxygen (`@return INVALID_CONFIG if ...`)

---

## Logging

- **Library code:** NO logging (not even optional)
- **Examples:** May use `examples/common/Log.h` (Arduino) or `printf` (ESP-IDF)
- **Never:** Log from ISRs

---

## Doxygen Documentation (Mandatory for Public API)

All public headers in `include/StatusLed/` require:

- **File:** `@file` + `@brief`
- **Class:** `@brief` + usage notes + threading/ISR constraints
- **Function:** `@brief` + `@param` + `@return` (what codes mean) + `@note`
  (side effects, validation, timing)
- **Config field:** `/// @brief` (purpose, units, valid range)

Keep it dense: state constraints, units, side effects. Omit the obvious.

---

## Testing Expectations

- Add host/unit tests (`pio test -e native -e native_max`) for timing/state transitions
- Tests must be deterministic and not rely on real time
- Cover edge cases (wraparound, mode transitions, temporary-preset lifecycle)
- Do not require hardware for unit tests
- Hardware smoke tests are optional and are not prerequisites for committing or
  pushing. Record when hardware validation has not been performed.
- When hardware testing is performed, smoke test `cli_esp32s3_idf5` (and
  `cli_esp32s3_idf` when the legacy backend changed): boot + basic LED output.

---

## Modification Process

**Before making changes, ask:**
> "Does this increase predictability and portability across projects?"

**If no, do not proceed.**

**Prefer:**
- Additive changes over breaking changes
- Optional features (Config flags) over mandatory changes
- Explicit behavior over implicit magic
- Refactoring a duplicated block over patching each copy

---

## Final Checklist

Before committing:
- [ ] Public API has Doxygen comments
- [ ] README documents behavior changes (modes, presets, threading, timing)
- [ ] Config struct has no hardcoded pins
- [ ] `tick()` is non-blocking and bounded
- [ ] Errors return Status, never silent
- [ ] No heap allocation in steady state
- [ ] No logging in library code
- [ ] Both CLIs still expose the same commands
- [ ] CHANGELOG.md updated
- [ ] `pio test -e native -e native_max` passes

**If any item fails, fix before proceeding.**
