/**
 * @file BackendConfig.h
 * @brief Compile-time backend selection for StatusLed.
 */

#pragma once

#include <stdint.h>

namespace StatusLed {

#if (defined(STATUSLED_BACKEND_IDF_WS2812) + defined(STATUSLED_BACKEND_NEOPIXELBUS) + \
     defined(STATUSLED_BACKEND_NULL)) == 0
#error "Select exactly one backend: STATUSLED_BACKEND_IDF_WS2812, STATUSLED_BACKEND_NEOPIXELBUS, or STATUSLED_BACKEND_NULL"
#endif

#if (defined(STATUSLED_BACKEND_IDF_WS2812) + defined(STATUSLED_BACKEND_NEOPIXELBUS) + \
     defined(STATUSLED_BACKEND_NULL)) > 1
#error "Multiple backends selected. Define only one of STATUSLED_BACKEND_IDF_WS2812, STATUSLED_BACKEND_NEOPIXELBUS, STATUSLED_BACKEND_NULL"
#endif

/// @brief Selected backend type.
enum class BackendType : uint8_t {
  IdfWs2812 = 0,
  NeoPixelBus = 1,
  Null = 2
};

#if defined(STATUSLED_BACKEND_IDF_WS2812)
static constexpr BackendType kSelectedBackend = BackendType::IdfWs2812;
#elif defined(STATUSLED_BACKEND_NEOPIXELBUS)
static constexpr BackendType kSelectedBackend = BackendType::NeoPixelBus;
#else
static constexpr BackendType kSelectedBackend = BackendType::Null;
#endif

}  // namespace StatusLed
