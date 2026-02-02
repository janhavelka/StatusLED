# StatusLED Audit Report

Date: 2026-02-02
Target: ESP32-S2 / ESP32-S3 (Arduino + PlatformIO)
Scope: `include/StatusLed/*`, `src/StatusLed.cpp`, `examples/01_status_led_cli`, `platformio.ini`

## Executive Summary

This audit reviewed the StatusLED subsystem for long-term robustness, non-blocking behavior, and failure safety. The engine is deterministic, bounded per tick, and avoids retransmitting static frames. The main runtime risks are related to backend driver conflicts (legacy vs next-gen RMT), and application misuse (calling from multiple threads or not calling `tick()` regularly). No unbounded loops or blocking delays were found in library code.

Critical fixes applied during audit:
- Added explicit validation for invalid `Mode` values to avoid undefined behavior.
- Hardened `getLedSnapshot()` against null output pointer.

## Architecture Overview

### Core Engine (StatusLed)
- Cooperative update pattern via `tick(now_ms)`.
- Per-LED state machine stored in a fixed-size `LedState` array (max 10 LEDs).
- Framebuffer `_frame[]` holds current RGB values; `_frameDirty` indicates pending transmit.
- Non-blocking scheduling: updates only when deadlines are reached. No `delay()` or busy-waits.
- Static modes (`Off`, `Solid`, `Dim`) stop scheduling further updates (`nextUpdateMs = kNever`).
- Animated modes update only on quantized step boundaries.

### Backends
- **Default**: NeoPixelBus (RMT-based) backend for hardware-timed output.
- **Fallback**: IDF WS2812 driver backend (`led_strip_new_rmt_ws2812`).
- **Test**: Null backend for host unit tests.

### Mode vs Color Separation
- Modes control temporal intensity only.
- Colors are configured via `setColor()` and `setSecondaryColor()`.
- Presets bundle a mode and colors but do not merge the two layers internally.

## Non-Blocking Behavior

- `tick()` is bounded and loops over `ledCount` only (<=10).
- `show()` is called only when `_frameDirty` is set.
- `NeoPixelBus` backend uses `CanShow()` before `Show()`.
- IDF backend uses `refresh(..., timeout=0)` and returns `RESOURCE_BUSY` instead of blocking.

## Failure and Stall Analysis

### 1. Driver Conflicts (Legacy vs RMT NG)
**Risk:** Arduino-ESP32 v3 can abort at boot if legacy RMT and RMT NG drivers are both linked.
**Current Behavior:** The library does not force linking NG; however, the application may inadvertently pull NG (e.g., board RGB helpers). This is a known integration risk.
**Mitigation:** Documentation advises switching backend defines if conflict occurs. No runtime detection is possible at library level.
**Residual Risk:** Medium (platform-level integration risk). Requires application testing on target.

### 2. Backend Allocation Failure
**Risk:** Heap allocation failure in `begin()` for backend or NeoPixelBus wrapper.
**Behavior:** `begin()` returns `OUT_OF_MEMORY`, library stays uninitialized, no crash.
**Residual Risk:** Low.

### 3. Invalid Configuration
**Risk:** Invalid pin, LED count, RMT channel, or smooth step range.
**Behavior:** `begin()` validates all; returns `INVALID_CONFIG`. No partial init.
**Residual Risk:** Low.

### 4. Invalid Mode Values
**Risk:** User passes invalid enum value (static_cast).
**Fix:** Added explicit validation; `setMode()` returns `INVALID_CONFIG`.
**Residual Risk:** Low.

### 5. Temporary Override Interaction
**Risk:** If `setMode()`/`setColor()` are called while a temporary preset is active, the temporary override will still revert to the previous stored state when its timer expires. This is deterministic but may be surprising.
**Mitigation:** Documented behavior. If needed, can change semantics to cancel temp overrides on manual changes.
**Residual Risk:** Low.

### 6. Long Gaps Between `tick()` Calls
**Risk:** If `tick()` is not called for long intervals, fade/blink phases may skip or complete immediately on the next tick (due to elapsed time exceeding period). This is deterministic and not a crash.
**Residual Risk:** Low. Expected in cooperative scheduler.

### 7. Frame Transmit Busy
**Risk:** Backend reports busy on refresh.
**Behavior:** Frame stays dirty and will retry on subsequent ticks. No blocking.
**Residual Risk:** Low.

### 8. Snapshot Reporting
**Risk:** `getLedSnapshot()` could be called with null pointer.
**Fix:** Now returns `INVALID_CONFIG` with `"out must not be null"`.
**Residual Risk:** Low.

### 9. Long-Term Stability (Years of Runtime)
- No heap allocation in `tick()`.
- No recursion, no unbounded loops.
- LED state arrays are static in class storage.
- Randomized modes use per-LED LFSR seeded on begin; deterministic and bounded.

## CPU and Timing Considerations

- Per tick work is O(ledCount). With <=10 LEDs this is negligible.
- Static modes stop scheduling; no further updates until a change occurs.
- Animated modes update on `smoothStepMs` boundaries (min 5ms).
- No retransmit when output is static.

## Error Handling Summary

All fallible operations return `Status`:
- `INVALID_CONFIG` for parameter issues.
- `NOT_INITIALIZED` when used before `begin()`.
- `RESOURCE_BUSY` for backend busy conditions.
- `HARDWARE_FAULT` for driver errors.

## Tests

Host unit tests (`pio test -e native`) validate:
- Blink fast toggling behavior.
- Temporary preset reversion.
- Fade-in one-shot completion.

Recommend adding target hardware smoke tests to verify RMT backend compatibility and driver conflicts under actual firmware integration.

## Findings and Recommendations

### High Priority
- **Driver conflict risk** remains the primary integration hazard. Recommend a hardware verification matrix for each board and firmware combination.

### Medium Priority
- Consider a runtime or compile-time guard in application code to avoid linking both legacy and NG RMT drivers.

### Low Priority
- Consider canceling temporary overrides when manual `setMode()`/`setColor()` is called, if desired by product UX.
- Optionally add snapshot tests for wraparound behavior near `millis()` overflow.

## Audit Changes Applied

- Added `Mode` validation in `setMode()`.
- Hardened `getLedSnapshot()` null pointer handling.

## Conclusion

The StatusLED subsystem is deterministic, non-blocking, and lightweight. Primary risks are integration-related (driver conflicts) rather than algorithmic. No crashes or stalls were found in the core engine under valid configuration and single-threaded use.
