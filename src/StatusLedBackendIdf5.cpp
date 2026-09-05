/**
 * @file StatusLedBackendIdf5.cpp
 * @brief WS2812 backend on the ESP-IDF RMT v2 TX driver (ESP-IDF 5.3+ / 6.x).
 */

#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#if STATUSLED_BACKEND_IDF5_WS2812

#include <stddef.h>
#include <stdlib.h>

#include <atomic>
#include <new>

extern "C" {
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "soc/soc_caps.h"
}

namespace StatusLed {
namespace {

// One RMT tick is 25 ns at this resolution, which is what the WS2812 bit
// timings below are expressed in. 80 MHz APB / 2 divides exactly.
constexpr uint32_t kRmtResolutionHz = 40000000;
constexpr uint32_t kTicksPerUs = kRmtResolutionHz / 1000000u;

constexpr uint16_t kT0H = 16;  // 0.40 us
constexpr uint16_t kT0L = 34;  // 0.85 us
constexpr uint16_t kT1H = 32;  // 0.80 us
constexpr uint16_t kT1L = 18;  // 0.45 us

// The channel's memory block must be a whole number of hardware blocks, and
// asking for more silently steals a neighbouring TX channel. 48 words on
// ESP32-S3, 64 on ESP32-S2.
constexpr size_t kMemBlockSymbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;

// Wait budget for draining the queue in end(). A full 10-LED frame is well
// under 1 ms, so this only matters if the driver is wedged.
constexpr int kCleanupWaitMs = 50;

constexpr uint8_t kMaxLeds = ::StatusLed::StatusLed::kMaxLedCount;
constexpr size_t kMaxPayloadBytes = static_cast<size_t>(kMaxLeds) * kBytesPerPixel;

bool isValidColorOrder(ColorOrder order) {
  switch (order) {
    case ColorOrder::GRB:
    case ColorOrder::RGB:
      return true;
    default:
      return false;
  }
}

/**
 * @brief Encoder emitting one WS2812 frame followed by the latch gap.
 *
 * The stock bytes encoder emits data bits only, and the driver's end-of-frame
 * marker has zero duration, so back-to-back transmissions would never latch.
 * This wraps the bytes encoder and appends a reset symbol, exactly like the
 * ESP-IDF led_strip encoder. `base` must stay the first member: the driver
 * hands the callbacks a pointer to it.
 */
struct Ws2812Encoder {
  rmt_encoder_t base;
  rmt_encoder_handle_t bytes;
  rmt_encoder_handle_t copy;
  rmt_symbol_word_t reset;
  int state;
};

size_t encodeFrame(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primaryData,
                   size_t dataSize, rmt_encode_state_t* retState) {
  Ws2812Encoder* self = reinterpret_cast<Ws2812Encoder*>(encoder);
  rmt_encode_state_t session = RMT_ENCODING_RESET;
  int state = RMT_ENCODING_RESET;
  size_t encoded = 0;

  if (self->state == 0) {
    encoded += self->bytes->encode(self->bytes, channel, primaryData, dataSize, &session);
    if (session & RMT_ENCODING_COMPLETE) {
      self->state = 1;
    }
    if (session & RMT_ENCODING_MEM_FULL) {
      *retState = RMT_ENCODING_MEM_FULL;
      return encoded;
    }
  }

  if (self->state == 1) {
    encoded += self->copy->encode(self->copy, channel, &self->reset, sizeof(self->reset), &session);
    if (session & RMT_ENCODING_COMPLETE) {
      self->state = 0;
      state |= RMT_ENCODING_COMPLETE;
    }
    if (session & RMT_ENCODING_MEM_FULL) {
      state |= RMT_ENCODING_MEM_FULL;
    }
  }

  *retState = static_cast<rmt_encode_state_t>(state);
  return encoded;
}

esp_err_t resetFrameEncoder(rmt_encoder_t* encoder) {
  Ws2812Encoder* self = reinterpret_cast<Ws2812Encoder*>(encoder);
  rmt_encoder_reset(self->bytes);
  rmt_encoder_reset(self->copy);
  self->state = 0;
  return ESP_OK;
}

esp_err_t deleteFrameEncoder(rmt_encoder_t* encoder) {
  Ws2812Encoder* self = reinterpret_cast<Ws2812Encoder*>(encoder);
  if (self->bytes != nullptr) {
    rmt_del_encoder(self->bytes);
  }
  if (self->copy != nullptr) {
    rmt_del_encoder(self->copy);
  }
  free(self);
  return ESP_OK;
}

/// @brief Build the composite WS2812 encoder (data bits + latch gap).
esp_err_t createFrameEncoder(rmt_encoder_handle_t* out) {
  Ws2812Encoder* enc = static_cast<Ws2812Encoder*>(rmt_alloc_encoder_mem(sizeof(Ws2812Encoder)));
  if (enc == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  enc->base.encode = &encodeFrame;
  enc->base.reset = &resetFrameEncoder;
  enc->base.del = &deleteFrameEncoder;
  enc->bytes = nullptr;
  enc->copy = nullptr;
  enc->state = 0;

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

  esp_err_t err = rmt_new_bytes_encoder(&bytesCfg, &enc->bytes);
  if (err != ESP_OK) {
    free(enc);
    return err;
  }

  rmt_copy_encoder_config_t copyCfg{};
  err = rmt_new_copy_encoder(&copyCfg, &enc->copy);
  if (err != ESP_OK) {
    rmt_del_encoder(enc->bytes);
    free(enc);
    return err;
  }

  // Split the latch gap over both halves of one symbol; each half is a 15-bit
  // field (max 32767 ticks = 819 us), and a zero duration is the stop marker.
  const uint16_t half = static_cast<uint16_t>(kTicksPerUs * (kResetUs / 2));
  enc->reset.level0 = 0;
  enc->reset.duration0 = half;
  enc->reset.level1 = 0;
  enc->reset.duration1 = half;

  *out = &enc->base;
  return ESP_OK;
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

    // RMT v2 allocates channels itself; Config::rmtChannel does not apply.
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
      _tx_chan = nullptr;
      return Status(Err::HARDWARE_FAULT, err, "rmt_new_tx_channel failed");
    }

    rmt_tx_event_callbacks_t txCallbacks{};
    txCallbacks.on_trans_done = &BackendIdf5Ws2812::onTxDone;
    err = rmt_tx_register_event_callbacks(_tx_chan, &txCallbacks, this);
    if (err != ESP_OK) {
      releaseDriver(gpio);
      return Status(Err::HARDWARE_FAULT, err, "rmt_tx_register_event_callbacks failed");
    }

    err = createFrameEncoder(&_encoder);
    if (err != ESP_OK) {
      releaseDriver(gpio);
      return Status(Err::HARDWARE_FAULT, err, "encoder alloc failed");
    }

    err = rmt_enable(_tx_chan);
    if (err != ESP_OK) {
      releaseDriver(gpio);
      return Status(Err::HARDWARE_FAULT, err, "rmt_enable failed");
    }

    _gpio = gpio;
    _count = config.ledCount;
    _txBusy.store(false, std::memory_order_release);
    _installed = true;
    return Ok();
  }

  void end() override {
    if (_installed && _tx_chan != nullptr && _encoder != nullptr && _count > 0) {
      // Leave the LEDs dark rather than stuck on the last frame.
      if (rmt_tx_wait_all_done(_tx_chan, kCleanupWaitMs) == ESP_OK) {
        for (size_t i = 0; i < kMaxPayloadBytes; ++i) {
          _payload[i] = 0;
        }
        if (transmit(static_cast<size_t>(_count) * kBytesPerPixel) == ESP_OK) {
          (void)rmt_tx_wait_all_done(_tx_chan, kCleanupWaitMs);
        }
      }
    }

    releaseDriver(_gpio);
    _installed = false;
    _count = 0;
    _gpio = GPIO_NUM_NC;
    _txBusy.store(false, std::memory_order_release);
  }

  bool canShow() const override {
    if (!_installed || _tx_chan == nullptr) {
      return false;
    }
    return !_txBusy.load(std::memory_order_acquire);
  }

  Status show(const RgbColor* frame, uint8_t count, ColorOrder order) override {
    if (!_installed || _tx_chan == nullptr || _encoder == nullptr) {
      return Status(Err::NOT_INITIALIZED, 0, "Backend not initialized");
    }
    if (frame == nullptr) {
      return Status(Err::INVALID_CONFIG, 0, "frame must not be null");
    }
    if (count == 0 || count > _count) {
      return Status(Err::INVALID_CONFIG, count, "count out of range");
    }
    if (!isValidColorOrder(order)) {
      return Status(Err::INVALID_CONFIG, static_cast<int32_t>(order), "invalid colorOrder");
    }
    // The payload must stay untouched until the transfer completes, and with a
    // queue depth of one rmt_transmit() would otherwise block.
    if (_txBusy.load(std::memory_order_acquire)) {
      return Status(Err::RESOURCE_BUSY, 0, "rmt busy");
    }

    for (uint8_t i = 0; i < count; ++i) {
      writePixelBytes(frame[i], order, &_payload[static_cast<size_t>(i) * kBytesPerPixel]);
    }

    _txBusy.store(true, std::memory_order_release);
    const esp_err_t err = transmit(static_cast<size_t>(count) * kBytesPerPixel);
    if (err != ESP_OK) {
      _txBusy.store(false, std::memory_order_release);
      if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT) {
        return Status(Err::RESOURCE_BUSY, err, "rmt busy");
      }
      return Status(Err::HARDWARE_FAULT, err, "rmt_transmit failed");
    }
    return Ok();
  }

 private:
  esp_err_t transmit(size_t payloadSize) {
    rmt_transmit_config_t txConfig{};
    txConfig.loop_count = 0;
    txConfig.flags.eot_level = 0;  // hold the data line low between frames
    return rmt_transmit(_tx_chan, _encoder, _payload, payloadSize, &txConfig);
  }

  /// @brief Tear down driver objects and leave the data line driven low.
  /// @note rmt_del_channel() calls gpio_reset_pin(), which leaves the pad as an
  ///       input with the pull-up enabled. A floating-high WS2812 data line can
  ///       latch noise, so drive it low afterwards.
  void releaseDriver(gpio_num_t gpio) {
    if (_tx_chan != nullptr) {
      (void)rmt_disable(_tx_chan);
    }
    if (_encoder != nullptr) {
      (void)rmt_del_encoder(_encoder);
      _encoder = nullptr;
    }
    if (_tx_chan != nullptr) {
      (void)rmt_del_channel(_tx_chan);
      _tx_chan = nullptr;
      if (gpio != GPIO_NUM_NC) {
        (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        (void)gpio_set_level(gpio, 0);
      }
    }
  }

  /// @brief Transmit-done callback. Runs in ISR context: only clears the flag.
  static bool onTxDone(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t* data,
                       void* userCtx) {
    (void)channel;
    (void)data;
    BackendIdf5Ws2812* self = static_cast<BackendIdf5Ws2812*>(userCtx);
    if (self != nullptr) {
      self->_txBusy.store(false, std::memory_order_release);
    }
    return false;  // no task woken
  }

  rmt_channel_handle_t _tx_chan = nullptr;
  rmt_encoder_handle_t _encoder = nullptr;
  gpio_num_t _gpio = GPIO_NUM_NC;
  bool _installed = false;
  uint8_t _count = 0;
  uint8_t _payload[kMaxPayloadBytes] = {};
  std::atomic<bool> _txBusy{false};
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
