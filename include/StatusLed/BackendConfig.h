/**
 * @file BackendConfig.h
 * @brief Compile-time backend selection for StatusLed.
 *
 * Pure ESP-IDF v6 component builds select STATUSLED_BACKEND_IDF5_WS2812.
 * Arduino/PlatformIO environments may still select legacy RMT, IDF5 RMT,
 * NeoPixelBus, or the null backend explicitly.
 */

#pragma once

#include <stdint.h>

namespace StatusLed {

#ifndef STATUSLED_BACKEND_IDF_WS2812
#define STATUSLED_BACKEND_IDF_WS2812 0
#endif

#ifndef STATUSLED_BACKEND_IDF5_WS2812
#define STATUSLED_BACKEND_IDF5_WS2812 0
#endif

#ifndef STATUSLED_BACKEND_NEOPIXELBUS
#define STATUSLED_BACKEND_NEOPIXELBUS 0
#endif

#ifndef STATUSLED_BACKEND_NULL
#define STATUSLED_BACKEND_NULL 0
#endif

#if (STATUSLED_BACKEND_IDF_WS2812 != 0 && STATUSLED_BACKEND_IDF_WS2812 != 1)
#error "STATUSLED_BACKEND_IDF_WS2812 must be 0 or 1"
#endif
#if (STATUSLED_BACKEND_IDF5_WS2812 != 0 && STATUSLED_BACKEND_IDF5_WS2812 != 1)
#error "STATUSLED_BACKEND_IDF5_WS2812 must be 0 or 1"
#endif
#if (STATUSLED_BACKEND_NEOPIXELBUS != 0 && STATUSLED_BACKEND_NEOPIXELBUS != 1)
#error "STATUSLED_BACKEND_NEOPIXELBUS must be 0 or 1"
#endif
#if (STATUSLED_BACKEND_NULL != 0 && STATUSLED_BACKEND_NULL != 1)
#error "STATUSLED_BACKEND_NULL must be 0 or 1"
#endif

#if (STATUSLED_BACKEND_IDF_WS2812 + STATUSLED_BACKEND_IDF5_WS2812 + STATUSLED_BACKEND_NEOPIXELBUS + STATUSLED_BACKEND_NULL) == 0
#error "Select exactly one backend: set one STATUSLED_BACKEND_* macro to 1"
#endif

#if (STATUSLED_BACKEND_IDF_WS2812 + STATUSLED_BACKEND_IDF5_WS2812 + STATUSLED_BACKEND_NEOPIXELBUS + STATUSLED_BACKEND_NULL) > 1
#error "Multiple backends selected. Set only one STATUSLED_BACKEND_* macro to 1"
#endif

/// @brief Compile-time selected LED output backend.
enum class BackendType : uint8_t {
  IdfWs2812 = 0,   ///< ESP-IDF 4.x RMT WS2812 backend.
  NeoPixelBus = 1, ///< NeoPixelBus backend.
  Null = 2,        ///< Host-test/no-hardware backend.
  Idf5Ws2812 = 3   ///< ESP-IDF 5.x RMT WS2812 backend.
};

#if STATUSLED_BACKEND_IDF_WS2812
/// @brief Backend selected by STATUSLED_BACKEND_* compile-time flags.
static constexpr BackendType kSelectedBackend = BackendType::IdfWs2812;
#elif STATUSLED_BACKEND_IDF5_WS2812
/// @brief Backend selected by STATUSLED_BACKEND_* compile-time flags.
static constexpr BackendType kSelectedBackend = BackendType::Idf5Ws2812;
#elif STATUSLED_BACKEND_NEOPIXELBUS
/// @brief Backend selected by STATUSLED_BACKEND_* compile-time flags.
static constexpr BackendType kSelectedBackend = BackendType::NeoPixelBus;
#else
/// @brief Backend selected by STATUSLED_BACKEND_* compile-time flags.
static constexpr BackendType kSelectedBackend = BackendType::Null;
#endif

}  // namespace StatusLed
