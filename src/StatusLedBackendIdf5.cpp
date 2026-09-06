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
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc_caps.h"
#ifndef SOC_RMT_CHANNELS_PER_GROUP
#include "hal/rmt_ll.h"
#endif
}

namespace StatusLed {
namespace {

// One RMT tick is 25 ns at this resolution, which is what the WS2812 bit
// timings below are expressed in. 80 MHz APB / 2 divides exactly.
constexpr uint32_t kRmtResolutionHz = 40000000;
constexpr uint32_t kTicksPerUs = kRmtResolutionHz / 1000000u;

// Wait budget for draining the queue in end(). Even a 255-LED frame is under
// 9 ms, so this only matters if the driver is wedged.
constexpr int kCleanupWaitMs = 50;

constexpr uint8_t kMaxLeds = ::StatusLed::StatusLed::kMaxLedCount;
constexpr size_t kMaxPayloadBytes = static_cast<size_t>(kMaxLeds) * kBytesPerPixel;

// IDF 6 moved the total channel count from SoC caps to the RMT HAL. Count all
// memory blocks, including RX blocks that the TX driver can reserve on S3.
#ifdef SOC_RMT_CHANNELS_PER_GROUP
constexpr uint16_t kRmtMemoryBlocks = SOC_RMT_CHANNELS_PER_GROUP;
#else
constexpr uint16_t kRmtMemoryBlocks = RMT_LL_GET(CHANS_PER_INST);
#endif

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

size_t IRAM_ATTR encodeFrame(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primaryData,
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

esp_err_t IRAM_ATTR resetFrameEncoder(rmt_encoder_t* encoder) {
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
  bytesCfg.bit0.duration0 = kRmtT0H;
  bytesCfg.bit0.level0 = 1;
  bytesCfg.bit0.duration1 = kRmtT0L;
  bytesCfg.bit0.level1 = 0;
  bytesCfg.bit1.duration0 = kRmtT1H;
  bytesCfg.bit1.level0 = 1;
  bytesCfg.bit1.duration1 = kRmtT1L;
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
  ~BackendIdf5Ws2812() override { end(); }

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

    // Full-frame buffering removes the refill deadline while flash cache is
    // disabled. It is opt-in because each additional block consumes another
    // channel's memory. The driver reserves those blocks, including RX blocks.
    const uint16_t blocks = config.rmtFullFrameBuffer
                                ? rmtMemoryBlocksForFrame(config.ledCount,
                                                          SOC_RMT_MEM_WORDS_PER_CHANNEL)
                                : 1;
    if (blocks > kRmtMemoryBlocks) {
      return Status(Err::INVALID_CONFIG, blocks, "not enough RMT memory blocks");
    }

    rmt_tx_channel_config_t txCfg{};
    txCfg.clk_src = RMT_CLK_SRC_DEFAULT;
    txCfg.gpio_num = gpio;
    txCfg.mem_block_symbols = static_cast<size_t>(blocks) * SOC_RMT_MEM_WORDS_PER_CHANNEL;
    txCfg.resolution_hz = kRmtResolutionHz;
    txCfg.trans_queue_depth = 1;
    txCfg.flags.invert_out = false;
    txCfg.flags.with_dma = false;

    esp_err_t err = rmt_new_tx_channel(&txCfg, &_tx_chan);
    if (err != ESP_OK) {
      _tx_chan = nullptr;
      if (err == ESP_ERR_NOT_FOUND) {
        return Status(Err::RESOURCE_BUSY, err, "no free RMT memory/channel");
      }
      if (err == ESP_ERR_NO_MEM) {
        return Status(Err::OUT_OF_MEMORY, err, "RMT allocation failed");
      }
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
    txConfig.flags.queue_nonblocking = true;
    return rmt_transmit(_tx_chan, _encoder, _payload, payloadSize, &txConfig);
  }

  /// @brief Tear down driver objects and leave the data line driven low.
  /// @note Driver GPIO cleanup differs across IDF releases; explicitly detach
  ///       RMT before driving low so later channel reuse cannot drive this pad.
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
        esp_rom_gpio_connect_out_signal(gpio, SIG_GPIO_OUT_IDX, false, false);
        (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        (void)gpio_set_level(gpio, 0);
      }
    }
  }

  /// @brief Transmit-done callback. Runs in ISR context: only clears the flag.
  static bool IRAM_ATTR onTxDone(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t* data,
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
  // Only acquire-load/release-store are used: Xtensa emits byte accesses and
  // memory barriers on S2/S3. ATOMIC_BOOL_LOCK_FREE also covers RMW operations
  // and is only 1 on S2, so it cannot validate this narrower ISR requirement.
  std::atomic<bool> _txBusy{false};
};

}  // namespace

BackendBase* createBackend() {
  // malloc/new's size threshold only prefers internal RAM; low internal memory
  // can otherwise put even this small object (payload + ISR context) in PSRAM.
  void* storage = heap_caps_malloc(sizeof(BackendIdf5Ws2812), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return storage == nullptr ? nullptr : new (storage) BackendIdf5Ws2812();
}

void destroyBackend(BackendBase* backend) {
  if (backend != nullptr) {
    auto* self = static_cast<BackendIdf5Ws2812*>(backend);
    self->~BackendIdf5Ws2812();
    heap_caps_free(self);
  }
}

}  // namespace StatusLed

#endif  // STATUSLED_BACKEND_IDF5_WS2812
