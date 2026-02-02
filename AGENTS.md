# AGENTS.md - Production Status LED Engineering Guidelines

## Role
You are a professional embedded software engineer building a production-grade status LED subsystem for ESP32.

**Primary goals:**
- Robustness and stability
- Deterministic, predictable behavior
- Zero boot-loops or stalls in production

**Target:** ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO.

**These rules are binding.**

---

## Repository Model (Single Library Template)

This repository is a SINGLE reusable library template designed to scale across multiple embedded projects:

### Folder Structure (Mandatory)

```
include/StatusLed/   - Public API headers ONLY (Doxygen documented)
  |-- Status.h       - Error types
  |-- Config.h       - Configuration struct
  |-- StatusLed.h    - Main library class
src/                 - Implementation (.cpp files)
examples/
  |-- 00_compile_only/       - Example applications
  |-- 01_status_led_cli/     - Interactive CLI demo
  |-- common/                - Example-only helpers (Log.h, BoardPins.h)
platformio.ini       - Build environments (uses build_src_filter)
library.json         - PlatformIO metadata
README.md           - Full documentation
CHANGELOG.md        - Keep a Changelog format
AGENTS.md           - This file
```

**Rules:**
- Public headers go in `include/StatusLed/` - these define the API contract
- Board-specific values (pins, etc.) NEVER in library code - only in `Config`
- Examples demonstrate usage - they may use `examples/common/BoardPins.h`
- Keep structure boring and predictable - no clever layouts

---

## Core Architecture Principles (Non-Negotiable)

### 1. Deterministic Behavior Over Convenience
- Predictable execution time
- No unbounded loops or waits
- All timeouts implemented via deadline checking (not delay())
- State machines preferred over "clever" event-driven code

### 2. Non-Blocking by Default

All libraries MUST expose:
```cpp
Status begin(const Config& config);  // Initialize
void tick(uint32_t now_ms);          // Cooperative update (non-blocking)
void end();                          // Cleanup
```

- `tick()` returns immediately after bounded work
- Long operations split into state machine steps
- Example: 120-second timeout -> check `now_ms >= deadline_ms` each tick

### 3. Explicit Configuration (No Hidden Globals)
- Hardware resources (pins, RMT channel, color order) passed via `Config`
- No hardcoded pins or interfaces in library code
- Libraries are board-agnostic by design
- Examples may provide board-specific defaults in `examples/common/BoardPins.h`

### 4. No Silent NVS / Storage Side Effects
- Persistent storage is OPTIONAL and DISABLED by default.
- Storage MUST be explicitly enabled by the user.
- When enabled:
  - all storage operations MUST be fallible (return `Status`);
  - write frequency MUST be controlled;
  - failures MUST not block or brick the system.
- Default behavior: zero storage access, zero side effects.

### 5. No Repeated Heap Allocations in Steady State
- Allocate resources in `begin()` if needed
- ZERO allocations in `tick()` and normal operation
- Use fixed-size buffers and ring buffers
- If allocation is unavoidable, document it clearly

### 6. Boring, Predictable Code
- Prefer verbose over clever
- Explicit state machines over callback chains
- Simple control flow over complex abstractions
- If uncertain, choose the simplest deterministic solution

---

## LED Subsystem Design Rules

### Output and Timing
- WS2812-class single-wire, 800kHz output
- No retransmit when output is static
  - Solid/static modes: no repeated sends
  - Blink: send only on toggles
  - Smooth fades: send only on quantized step changes
- No delay() or busy-waiting in library code

### CPU and Watchdog Safety
- Per-tick work is bounded and minimal
- Only call `show()` when frame is dirty and backend is ready
- Coalesce updates if a transmit is in progress

### Backends and Driver Safety
- Prefer hardware-driven output (RMT)
- Pin dependency versions in `platformio.ini` `lib_deps`
- Avoid mixing legacy and next-gen RMT drivers in the same build
- Provide a fallback backend if driver conflicts are observed
- Never ship a configuration that can boot-loop or abort at runtime

---

## Error Handling

### Status/Err Type (Mandatory)
- Library APIs return `Status` struct:
  ```cpp
  struct Status {
    Err code;           // Category (OK, INVALID_CONFIG, TIMEOUT, ...)
    int32_t detail;     // Vendor/third-party error code
    const char* msg;    // STATIC STRING ONLY (never heap-allocated)
  };
  ```
- When wrapping third-party libraries, translate errors at boundary
- Store original error code in `detail` field
- Silent failure is unacceptable - always return Status

### Error Propagation
- Errors must be checkable: `if (!status.ok()) { /* handle */ }`
- Log errors in examples, not in library code
- Document error conditions in Doxygen (`@return INVALID_CONFIG if ...`)

---

## Configuration Rules

### Config Struct Design
```cpp
struct Config {
  // Hardware
  int dataPin = -1;         // -1 = disabled/not used
  uint8_t ledCount = 0;     // 1..10
  uint8_t rmtChannel = 0;   // ESP32-S2/S3: 0..3
  ColorOrder colorOrder = ColorOrder::GRB;

  // Behavior
  uint32_t tickMinStepMs = 20;   // quantized updates for smooth modes
  uint8_t globalBrightness = 255; // 0..255
};
```

**Rules:**
- All pins default to -1 (disabled)
- All timeouts in milliseconds (uint32_t)
- Boolean flags for optional features
- Validate in `begin()`, return `INVALID_CONFIG` on error
- Document valid ranges in Doxygen

---

## Logging

- **Library code:** NO logging (not even optional)
- **Examples:** May use `examples/common/Log.h` macros
- **Never:** Log from ISRs
- **Production:** Libraries must work without Serial/logging

---

## Doxygen Documentation (Mandatory for Public API)

All public headers in `include/StatusLed/` require:

**File:** `@file` + `@brief` (one line) + optional detail paragraph

**Class:** `@brief` (what it does) + usage notes + threading/ISR constraints

**Function:** `@brief` + `@param` (name + meaning) + `@return` (what codes mean) + `@note` (side effects, validation, timing)

**Config field:** `/// @brief` (purpose, units, valid range) + `@note` if pin is application-provided

**Keep it dense:** State what matters (constraints, units, side effects). Omit obvious explanations.

---

## README Behavioral Contracts (Required Sections)

When adding/modifying functionality, update README with:

1. **Threading Model:** "Single-threaded by default. No internal tasks."
2. **Timing:** "tick() completes in <1ms. Long operations split across calls."
3. **Resource Ownership:** "LED pin and RMT channel passed via Config. No hardcoded resources."
4. **Memory:** "All allocation in begin(). Zero allocation in tick()."
5. **Error Handling:** "All errors returned as Status. No silent failures."

---

## Testing Expectations

- Add host/unit tests for timing/state transitions
- Tests must be deterministic and not rely on real time
- Keep test coverage for edge cases (wraparound, mode transitions)
- Do not require hardware for unit tests

---

## Modification Process

**Before making changes, ask:**
> "Does this increase predictability and portability across projects?"

**If no, do not proceed.**

**When adding features:**
1. Output intended file tree changes (short summary)
2. Apply edits (prefer additive changes over refactors)
3. Update documentation (README + Doxygen)
4. Summarize in <=10 bullets

**Prefer:**
- Additive changes over breaking changes
- Optional features (Config flags) over mandatory changes
- Explicit behavior over implicit magic

---

## Final Checklist

Before committing:
- [ ] Public API has Doxygen comments
- [ ] README documents threading and timing model
- [ ] Config struct has no hardcoded pins
- [ ] `tick()` is non-blocking and bounded
- [ ] Errors return Status, never silent
- [ ] No heap allocation in steady state
- [ ] No logging in library code
- [ ] Examples demonstrate correct usage
- [ ] CHANGELOG.md updated

**If any item fails, fix before proceeding.**
