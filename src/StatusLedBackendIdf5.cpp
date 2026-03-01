/**
 * @file StatusLedBackendIdf5.cpp
 * @brief IDF RMT v2 backend for StatusLed (ESP-IDF 5.x).
 */

#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#if STATUSLED_BACKEND_IDF5_WS2812

#include <stddef.h>
#include <new>

extern "C" {
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
}

namespace StatusLed {
namespace {

static bool isValidColorOrder(ColorOrder order) {
  switch (order) {
    case ColorOrder::GRB:
    case ColorOrder::RGB:
      return true;
    default:
      return false;
  }
}

class BackendIdf5Ws2812 final : public BackendBase {
 public:
  Status begin(const Config& config) override {
    end();

    if (config.dataPin < 0) {
      return Status(Err::INVALID_CONFIG, config.dataPin, "dataPin must be >= 0");
    }
    if (config.ledCount == 0 || config.ledCount > kMaxLeds) {
      return Status(Err::INVALID_CONFIG, config.ledCount, "ledCount out of range");
    }

    const gpio_num_t gpio = static_cast<gpio_num_t>(config.dataPin);
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
      return Status(Err::INVALID_CONFIG, config.dataPin, "dataPin is not a valid output GPIO");
    }

    // Channel allocation is dynamic in IDF 5.x. Config.rmtChannel is ignored by this backend.
    (void)config.rmtChannel;

    rmt_tx_channel_config_t txCfg{};
    txCfg.clk_src = RMT_CLK_SRC_DEFAULT;
    txCfg.gpio_num = gpio;
    txCfg.mem_block_symbols = kMemBlockSymbols;
    txCfg.resolution_hz = kRmtResolutionHz;
    txCfg.trans_queue_depth = 1;
    txCfg.flags.invert_out = false;
    txCfg.flags.with_dma = false;

    esp_err_t err = rmt_new_tx_channel(&txCfg, &_tx_chan);
    if (err != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, err, "rmt_new_tx_channel failed");
    }

    rmt_tx_event_callbacks_t txCallbacks{};
    txCallbacks.on_trans_done = &BackendIdf5Ws2812::onTxDone;
    err = rmt_tx_register_event_callbacks(_tx_chan, &txCallbacks, this);
    if (err != ESP_OK) {
      (void)rmt_del_channel(_tx_chan);
      _tx_chan = nullptr;
      return Status(Err::HARDWARE_FAULT, err, "rmt_tx_register_event_callbacks failed");
    }

    rmt_bytes_encoder_config_t bytesCfg{};
    bytesCfg.bit0.duration0 = kT0H;
    bytesCfg.bit0.level0 = 1;
    bytesCfg.bit0.duration1 = kT0L;
    bytesCfg.bit0.level1 = 0;
    bytesCfg.bit1.duration0 = kT1H;
    bytesCfg.bit1.level0 = 1;
    bytesCfg.bit1.duration1 = kT1L;
    bytesCfg.bit1.level1 = 0;
    bytesCfg.flags.msb_first = true;

    err = rmt_new_bytes_encoder(&bytesCfg, &_bytes_encoder);
    if (err != ESP_OK) {
      (void)rmt_del_channel(_tx_chan);
      _tx_chan = nullptr;
      return Status(Err::HARDWARE_FAULT, err, "rmt_new_bytes_encoder failed");
    }

    err = rmt_enable(_tx_chan);
    if (err != ESP_OK) {
      (void)rmt_del_encoder(_bytes_encoder);
      (void)rmt_del_channel(_tx_chan);
      _bytes_encoder = nullptr;
      _tx_chan = nullptr;
      return Status(Err::HARDWARE_FAULT, err, "rmt_enable failed");
    }

    _count = config.ledCount;
    _txBusy = false;
    _installed = true;
    return Ok();
  }

  void end() override {
    if (_installed && _tx_chan != nullptr && _bytes_encoder != nullptr && _count > 0) {
      if (rmt_tx_wait_all_done(_tx_chan, kCleanupWaitMs) == ESP_OK) {
        uint8_t blank[kMaxPayloadBytes]{};
        rmt_transmit_config_t txConfig{};
        txConfig.loop_count = 0;
        txConfig.flags.eot_level = 0;
        const size_t payloadSize = static_cast<size_t>(_count) * kBytesPerLed;
        const esp_err_t txErr =
            rmt_transmit(_tx_chan, _bytes_encoder, blank, payloadSize, &txConfig);
        if (txErr == ESP_OK) {
          (void)rmt_tx_wait_all_done(_tx_chan, kCleanupWaitMs);
        }
      }
    }

    if (_tx_chan != nullptr) {
      (void)rmt_disable(_tx_chan);
    }
    if (_bytes_encoder != nullptr) {
      (void)rmt_del_encoder(_bytes_encoder);
      _bytes_encoder = nullptr;
    }
    if (_tx_chan != nullptr) {
      (void)rmt_del_channel(_tx_chan);
      _tx_chan = nullptr;
    }

    _installed = false;
    _count = 0;
    _txBusy = false;
  }

  bool canShow() const override {
    if (!_installed || _tx_chan == nullptr) {
      return false;
    }
    return !_txBusy;
  }

  Status show(const RgbColor* frame, uint8_t count, ColorOrder order) override {
    if (!_installed || _tx_chan == nullptr || _bytes_encoder == nullptr) {
      return Status(Err::NOT_INITIALIZED, 0, "Backend not initialized");
    }
    if (frame == nullptr) {
      return Status(Err::INVALID_CONFIG, 0, "frame must not be null");
    }
    if (count == 0 || count > kMaxLeds) {
      return Status(Err::INVALID_CONFIG, count, "count out of range");
    }
    if (count > _count) {
      return Status(Err::INVALID_CONFIG, count, "count exceeds configured ledCount");
    }
    if (!isValidColorOrder(order)) {
      return Status(Err::INVALID_CONFIG, static_cast<int32_t>(order), "invalid colorOrder");
    }
    if (_txBusy) {
      return Status(Err::RESOURCE_BUSY, 0, "rmt busy");
    }

    uint8_t payload[kMaxPayloadBytes]{};
    size_t offset = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const RgbColor mapped = mapColorOrder(frame[i], ColorOrder::RGB, order);
      payload[offset++] = mapped.r;
      payload[offset++] = mapped.g;
      payload[offset++] = mapped.b;
    }

    rmt_transmit_config_t txConfig{};
    txConfig.loop_count = 0;
    txConfig.flags.eot_level = 0;
    const size_t payloadSize = static_cast<size_t>(count) * kBytesPerLed;
    _txBusy = true;
    const esp_err_t err = rmt_transmit(_tx_chan, _bytes_encoder, payload, payloadSize, &txConfig);
    if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT) {
      _txBusy = false;
      return Status(Err::RESOURCE_BUSY, err, "rmt busy");
    }
    if (err != ESP_OK) {
      _txBusy = false;
      return Status(Err::HARDWARE_FAULT, err, "rmt_transmit failed");
    }

    return Ok();
  }

 private:
  static bool onTxDone(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t* data, void* userCtx) {
    (void)channel;
    (void)data;
    BackendIdf5Ws2812* self = static_cast<BackendIdf5Ws2812*>(userCtx);
    if (self != nullptr) {
      self->_txBusy = false;
    }
    return false;
  }

  static constexpr uint32_t kRmtResolutionHz = 40000000;   // 40MHz
  static constexpr uint16_t kT0H = 16;                     // 0.4us
  static constexpr uint16_t kT0L = 34;                     // 0.85us
  static constexpr uint16_t kT1H = 32;                     // 0.8us
  static constexpr uint16_t kT1L = 18;                     // 0.45us
  static constexpr uint32_t kMemBlockSymbols = 64;
  static constexpr uint32_t kCleanupWaitMs = 10;
  static constexpr uint8_t kMaxLeds = ::StatusLed::StatusLed::kMaxLedCount;
  static constexpr size_t kBytesPerLed = 3;
  static constexpr size_t kMaxPayloadBytes = kMaxLeds * kBytesPerLed;

  rmt_channel_handle_t _tx_chan = nullptr;
  rmt_encoder_handle_t _bytes_encoder = nullptr;
  bool _installed = false;
  uint8_t _count = 0;
  volatile bool _txBusy = false;
};

}  // namespace

BackendBase* createBackend() {
  return new (std::nothrow) BackendIdf5Ws2812();
}

void destroyBackend(BackendBase* backend) {
  delete backend;
}

}  // namespace StatusLed

#endif  // STATUSLED_BACKEND_IDF5_WS2812
