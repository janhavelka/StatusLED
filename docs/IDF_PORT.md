# StatusLED — ESP-IDF Migration Prompt

> **Library**: StatusLED (WS2812 / SK6812 LED strip driver)
> **Current version**: 1.2.0 → **Target**: 2.0.0
> **Namespace**: `StatusLed`
> **Include path**: `#include "StatusLed/StatusLed.h"`
> **Difficulty**: Trivial — already framework-agnostic, packaging only

---

## Pre-Migration

```bash
git tag v1.2.0   # freeze Arduino-era version
```

---

## Current State

Zero Arduino dependencies. Four compile-time backends already exist:

| Backend      | Guard define               | Status            |
|------------- |--------------------------- |-------------------|
| IDF 4.x RMT  | `STATUSLED_BACKEND_IDF4`   | Keep for reference |
| IDF 5.x RMT  | `STATUSLED_BACKEND_IDF5`   | **Default for v2** |
| NeoPixelBus  | `STATUSLED_BACKEND_NPB`    | Remove             |
| Null/stub    | `STATUSLED_BACKEND_NULL`   | Keep for tests     |

Time fully injected via `tick(uint32_t nowMs)`. Config is struct-based.

---

## Steps

### 1. Set IDF5 as default backend

In the build/config system, make `STATUSLED_BACKEND_IDF5` the default. Remove `STATUSLED_BACKEND_NPB` (NeoPixelBus) backend code — it depends on an Arduino library.

### 2. Remove NeoPixelBus backend

Delete the NeoPixelBus backend source file and any `#if STATUSLED_BACKEND_NPB` blocks.

### 3. Fix stale `Version.h`

Current `Version.h` says 1.1.0. Update to `2.0.0`.

### 4. Add `CMakeLists.txt` (library root)

```cmake
idf_component_register(
    SRCS "src/StatusLed.cpp"
         "src/StatusLedBackendIdf5.cpp"
         "src/StatusLedBackendNull.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
```

Adjust `SRCS` to match actual filenames. Keep IDF4 backend only if needed; otherwise remove it.

### 5. Add `idf_component.yml` (library root)

```yaml
version: "2.0.0"
description: "WS2812/SK6812 LED strip driver — ESP-IDF RMT backend"
targets:
  - esp32s2
  - esp32s3
dependencies:
  idf: ">=5.0"
```

### 6. Bump `library.json` version to `2.0.0`

### 7. Replace Arduino example with ESP-IDF example

Create `examples/espidf_basic/main/main.cpp`:

```cpp
#include <cstdio>
#include "StatusLed/StatusLed.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
    StatusLed::Config cfg{};
    cfg.pin = 18;           // GPIO for WS2812 data line
    cfg.numLeds = 8;

    StatusLed::Strip strip;
    auto st = strip.begin(cfg);
    if (st.err != StatusLed::Err::Ok) {
        printf("begin() failed: %s\n", st.msg);
        return;
    }

    while (true) {
        uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
        strip.tick(nowMs);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

Create `examples/espidf_basic/main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES driver esp_timer)
```

Create `examples/espidf_basic/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(statusled_espidf_basic)
```

---

## Verification

```bash
cd examples/espidf_basic && idf.py set-target esp32s2 && idf.py build
```

- [ ] `idf.py build` succeeds with IDF5 backend
- [ ] NeoPixelBus backend fully removed
- [ ] Zero `#include <Arduino.h>` anywhere
- [ ] Version.h, library.json, idf_component.yml all say 2.0.0
- [ ] `git tag v2.0.0`
