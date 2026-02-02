/**
 * @file BackendConfig.h
 * @brief Compile-time backend selection for StatusLed.
 */

#pragma once

#include <stdint.h>

namespace StatusLed {

#ifndef STATUSLED_BACKEND_IDF_WS2812
#define STATUSLED_BACKEND_IDF_WS2812 0
#else
#if ((STATUSLED_BACKEND_IDF_WS2812 + 0) == 0)
#undef STATUSLED_BACKEND_IDF_WS2812
#define STATUSLED_BACKEND_IDF_WS2812 0
#elif ((STATUSLED_BACKEND_IDF_WS2812 + 0) == 1)
#undef STATUSLED_BACKEND_IDF_WS2812
#define STATUSLED_BACKEND_IDF_WS2812 1
#else
#error "STATUSLED_BACKEND_IDF_WS2812 must be 0 or 1"
#endif
#endif

#ifndef STATUSLED_BACKEND_NEOPIXELBUS
#define STATUSLED_BACKEND_NEOPIXELBUS 0
#else
#if ((STATUSLED_BACKEND_NEOPIXELBUS + 0) == 0)
#undef STATUSLED_BACKEND_NEOPIXELBUS
#define STATUSLED_BACKEND_NEOPIXELBUS 0
#elif ((STATUSLED_BACKEND_NEOPIXELBUS + 0) == 1)
#undef STATUSLED_BACKEND_NEOPIXELBUS
#define STATUSLED_BACKEND_NEOPIXELBUS 1
#else
#error "STATUSLED_BACKEND_NEOPIXELBUS must be 0 or 1"
#endif
#endif

#ifndef STATUSLED_BACKEND_NULL
#define STATUSLED_BACKEND_NULL 0
#else
#if ((STATUSLED_BACKEND_NULL + 0) == 0)
#undef STATUSLED_BACKEND_NULL
#define STATUSLED_BACKEND_NULL 0
#elif ((STATUSLED_BACKEND_NULL + 0) == 1)
#undef STATUSLED_BACKEND_NULL
#define STATUSLED_BACKEND_NULL 1
#else
#error "STATUSLED_BACKEND_NULL must be 0 or 1"
#endif
#endif

#if (STATUSLED_BACKEND_IDF_WS2812 + STATUSLED_BACKEND_NEOPIXELBUS + STATUSLED_BACKEND_NULL) == 0
#error "Select exactly one backend: set one STATUSLED_BACKEND_* macro to 1"
#endif

#if (STATUSLED_BACKEND_IDF_WS2812 + STATUSLED_BACKEND_NEOPIXELBUS + STATUSLED_BACKEND_NULL) > 1
#error "Multiple backends selected. Set only one STATUSLED_BACKEND_* macro to 1"
#endif

/// @brief Selected backend type.
enum class BackendType : uint8_t {
  IdfWs2812 = 0,
  NeoPixelBus = 1,
  Null = 2
};

#if STATUSLED_BACKEND_IDF_WS2812
static constexpr BackendType kSelectedBackend = BackendType::IdfWs2812;
#elif STATUSLED_BACKEND_NEOPIXELBUS
static constexpr BackendType kSelectedBackend = BackendType::NeoPixelBus;
#else
static constexpr BackendType kSelectedBackend = BackendType::Null;
#endif

}  // namespace StatusLed
