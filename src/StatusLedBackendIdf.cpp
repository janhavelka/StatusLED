/**
 * @file StatusLedBackendIdf.cpp
 * @brief IDF RMT backend for StatusLed (legacy driver).
 */

#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#if STATUSLED_BACKEND_IDF_WS2812

#include <new>

extern "C" {
#include "driver/rmt.h"
}

namespace StatusLed {
namespace {

class BackendIdfWs2812 final : public BackendBase {
 public:
  Status begin(const Config& config) override {
    end();

    _channel = static_cast<rmt_channel_t>(config.rmtChannel);

    rmt_config_t rmt_cfg = RMT_DEFAULT_CONFIG_TX(static_cast<gpio_num_t>(config.dataPin), _channel);
    rmt_cfg.clk_div = kRmtClkDiv;
    rmt_cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    rmt_cfg.tx_config.idle_output_en = true;
    rmt_cfg.tx_config.carrier_en = false;

    esp_err_t err = rmt_config(&rmt_cfg);
    if (err != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, err, "rmt_config failed");
    }

    err = rmt_driver_install(_channel, 0, 0);
    if (err != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, err, "rmt_driver_install failed");
    }

    _installed = true;
    return Ok();
  }

  void end() override {
    if (_installed) {
      rmt_driver_uninstall(_channel);
      _installed = false;
    }
  }

  bool canShow() const override {
    if (!_installed) {
      return false;
    }
    return rmt_wait_tx_done(_channel, 0) == ESP_OK;
  }

  Status show(const RgbColor* frame, uint8_t count, ColorOrder order) override {
    if (!_installed) {
      return Status(Err::NOT_INITIALIZED, 0, "Backend not initialized");
    }

    if (rmt_wait_tx_done(_channel, 0) == ESP_ERR_TIMEOUT) {
      return Status(Err::RESOURCE_BUSY, 0, "rmt busy");
    }

    const size_t itemCount = buildItems(frame, count, order);
    if (itemCount == 0) {
      return Status(Err::INTERNAL_ERROR, 0, "item build failed");
    }

    const esp_err_t err = rmt_write_items(_channel, _items, static_cast<int>(itemCount), false);
    if (err != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, err, "rmt_write_items failed");
    }
    return Ok();
  }

 private:
  static constexpr uint8_t kRmtClkDiv = 2;          // 80MHz / 2 = 40MHz
  static constexpr uint16_t kT0H = 16;              // 0.4us
  static constexpr uint16_t kT0L = 34;              // 0.85us
  static constexpr uint16_t kT1H = 32;              // 0.8us
  static constexpr uint16_t kT1L = 18;              // 0.45us
  static constexpr uint16_t kResetTicks = 3200;     // 80us
  static constexpr uint8_t kMaxLeds = ::StatusLed::StatusLed::kMaxLedCount;
  static constexpr uint16_t kBitsPerLed = 24;
  static constexpr uint16_t kMaxItems = (kMaxLeds * kBitsPerLed) + 1;

  size_t buildItems(const RgbColor* frame, uint8_t count, ColorOrder order) {
    if (count == 0 || count > kMaxLeds) {
      return 0;
    }

    size_t idx = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const RgbColor mapped = mapColorOrder(frame[i], ColorOrder::RGB, order);
      encodeByte(mapped.r, idx);
      encodeByte(mapped.g, idx);
      encodeByte(mapped.b, idx);
    }

    if (idx >= kMaxItems) {
      return 0;
    }

    rmt_item32_t& reset = _items[idx++];
    reset.level0 = 0;
    reset.duration0 = kResetTicks;
    reset.level1 = 0;
    reset.duration1 = 0;

    return idx;
  }

  void encodeByte(uint8_t value, size_t& idx) {
    for (int bit = 7; bit >= 0; --bit) {
      const bool isOne = (value >> bit) & 0x1;
      rmt_item32_t& item = _items[idx++];
      item.level0 = 1;
      item.duration0 = isOne ? kT1H : kT0H;
      item.level1 = 0;
      item.duration1 = isOne ? kT1L : kT0L;
    }
  }

  rmt_item32_t _items[kMaxItems]{};
  rmt_channel_t _channel = RMT_CHANNEL_0;
  bool _installed = false;
};

}  // namespace

BackendBase* createBackend() {
  return new (std::nothrow) BackendIdfWs2812();
}

void destroyBackend(BackendBase* backend) {
  delete backend;
}

}  // namespace StatusLed

#endif  // STATUSLED_BACKEND_IDF_WS2812
