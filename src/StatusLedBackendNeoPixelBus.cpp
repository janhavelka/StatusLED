/**
 * @file StatusLedBackendNeoPixelBus.cpp
 * @brief NeoPixelBus backend for StatusLed.
 */

#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#if STATUSLED_BACKEND_NEOPIXELBUS

#include <esp_idf_version.h>

// NeoPixelBus drives the LEDs through the legacy <driver/rmt.h> API. The pinned
// 2.7.6 needs Arduino-ESP32 2.x (ESP-IDF 4.4); 2.8.0 and later build on
// ESP-IDF 5.x only through the deprecated RMT shim, and that shim is gone in
// ESP-IDF 6.0. Fail here rather than deep inside the dependency.
#if ESP_IDF_VERSION_MAJOR >= 5
#error "STATUSLED_BACKEND_NEOPIXELBUS requires Arduino-ESP32 2.x (ESP-IDF 4.4). Select STATUSLED_BACKEND_IDF5_WS2812 instead."
#endif

#include <NeoPixelBus.h>
#include <new>

namespace StatusLed {
namespace {

class NeoPixelBusWrapperBase {
 public:
  virtual ~NeoPixelBusWrapperBase() = default;
  virtual void begin() = 0;
  virtual bool canShow() const = 0;
  virtual void show() = 0;
  virtual void clear() = 0;
  virtual void setPixel(uint16_t index, const ::RgbColor& color) = 0;
};

template <typename Method>
class NeoPixelBusWrapper final : public NeoPixelBusWrapperBase {
 public:
  NeoPixelBusWrapper(uint16_t count, uint8_t pin) : _bus(count, pin) {}

  void begin() override { _bus.Begin(); }
  bool canShow() const override { return _bus.CanShow(); }
  void show() override { _bus.Show(); }
  void clear() override { _bus.ClearTo(::RgbColor(0)); }
  void setPixel(uint16_t index, const ::RgbColor& color) override { _bus.SetPixelColor(index, color); }

 private:
  NeoPixelBus<NeoGrbFeature, Method> _bus;
};

class BackendNeoPixelBus final : public BackendBase {
 public:
  Status begin(const Config& config) override {
    end();
    if (config.dataPin < 0 || config.dataPin > 255) {
      return Status(Err::INVALID_CONFIG, config.dataPin, "dataPin out of range");
    }
    if (config.ledCount == 0) {
      return Status(Err::INVALID_CONFIG, 0, "ledCount must be > 0");
    }
    _pin = static_cast<uint8_t>(config.dataPin);
    _count = config.ledCount;

    switch (config.rmtChannel) {
      case 0:
        _bus = new (std::nothrow) NeoPixelBusWrapper<NeoEsp32Rmt0Ws2812xMethod>(_count, _pin);
        break;
      case 1:
        _bus = new (std::nothrow) NeoPixelBusWrapper<NeoEsp32Rmt1Ws2812xMethod>(_count, _pin);
        break;
      case 2:
        _bus = new (std::nothrow) NeoPixelBusWrapper<NeoEsp32Rmt2Ws2812xMethod>(_count, _pin);
        break;
      case 3:
        _bus = new (std::nothrow) NeoPixelBusWrapper<NeoEsp32Rmt3Ws2812xMethod>(_count, _pin);
        break;
      default:
        return Status(Err::INVALID_CONFIG, config.rmtChannel, "Invalid RMT channel");
    }

    if (_bus == nullptr) {
      return Status(Err::OUT_OF_MEMORY, 0, "NeoPixelBus alloc failed");
    }

    _bus->begin();
    return Ok();
  }

  void end() override {
    if (_bus) {
      _bus->clear();
      if (_bus->canShow()) {
        _bus->show();
      }
      delete _bus;
      _bus = nullptr;
    }
  }

  bool canShow() const override { return _bus ? _bus->canShow() : false; }

  Status show(const RgbColor* frame, uint8_t count, ColorOrder order) override {
    if (_bus == nullptr) {
      return Status(Err::NOT_INITIALIZED, 0, "Backend not initialized");
    }
    if (frame == nullptr) {
      return Status(Err::INVALID_CONFIG, 0, "frame must not be null");
    }
    if (count == 0 || count > _count) {
      return Status(Err::INVALID_CONFIG, count, "count out of range");
    }
    if (!_bus->canShow()) {
      return Status(Err::RESOURCE_BUSY, 0, "NeoPixelBus busy");
    }

    // NeoGrbFeature already emits green first, so a GRB strip takes the
    // logical color unchanged. For an RGB strip, pre-swap red and green so the
    // feature's GRB ordering puts them back in RGB order on the wire.
    for (uint8_t i = 0; i < count; ++i) {
      const RgbColor& c = frame[i];
      _bus->setPixel(i, (order == ColorOrder::GRB) ? ::RgbColor(c.r, c.g, c.b)
                                                   : ::RgbColor(c.g, c.r, c.b));
    }
    _bus->show();
    return Ok();
  }

 private:
  NeoPixelBusWrapperBase* _bus = nullptr;
  uint8_t _count = 0;
  uint8_t _pin = 0;
};

}  // namespace

BackendBase* createBackend() {
  return new (std::nothrow) BackendNeoPixelBus();
}

void destroyBackend(BackendBase* backend) {
  delete backend;
}

}  // namespace StatusLed

#endif  // STATUSLED_BACKEND_NEOPIXELBUS
