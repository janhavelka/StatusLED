/**
 * @file StatusLedBackendIdf.cpp
 * @brief WS2812 backend on the legacy ESP-IDF RMT driver (ESP-IDF 4.4 /
 *        Arduino-ESP32 2.x). Removed in ESP-IDF 6.x.
 */

#include "StatusLedBackend.h"
#include "StatusLedInternal.h"

#if STATUSLED_BACKEND_IDF_WS2812

#include <new>

extern "C" {
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc_caps.h"
}

namespace StatusLed {
namespace {

class BackendIdfWs2812 final : public BackendBase {
 public:
  ~BackendIdfWs2812() override { end(); }

  Status begin(const Config& config) override {
    end();

    if (config.dataPin < 0) {
      return Status(Err::INVALID_CONFIG, config.dataPin, "dataPin must be >= 0");
    }
    if (config.ledCount == 0 || config.ledCount > kMaxLeds) {
      return Status(Err::INVALID_CONFIG, config.ledCount, "ledCount out of range");
    }
    if (config.rmtChannel > kMaxTxChannel) {
      return Status(Err::INVALID_CONFIG, config.rmtChannel, "rmtChannel is not TX capable");
    }

    const gpio_num_t gpio = static_cast<gpio_num_t>(config.dataPin);
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
      return Status(Err::INVALID_CONFIG, config.dataPin, "dataPin is not a valid output GPIO");
    }
    _channel = static_cast<rmt_channel_t>(config.rmtChannel);

    const uint16_t blocks = config.rmtFullFrameBuffer
                                ? rmtMemoryBlocksForFrame(config.ledCount,
                                                          SOC_RMT_MEM_WORDS_PER_CHANNEL)
                                : 1;
    // The legacy driver's config check is hardcoded to eight even on S2.
    if (config.rmtChannel + blocks > SOC_RMT_CHANNELS_PER_GROUP) {
      return Status(Err::INVALID_CONFIG, blocks, "not enough RMT memory blocks");
    }

    // The legacy driver does not reserve borrowed memory blocks. Check all
    // installed channels before configuring ours, including RX allocations.
    // Applications must serialize RMT resource initialization and teardown.
    rmt_channel_status_result_t channels{};
    esp_err_t err = rmt_get_channel_status(&channels);
    if (err != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, err, "rmt_get_channel_status failed");
    }
    for (uint8_t ch = 0; ch < SOC_RMT_CHANNELS_PER_GROUP; ++ch) {
      if (channels.status[ch] == RMT_CHANNEL_UNINIT) {
        continue;
      }
      uint8_t usedBlocks = 0;
      err = rmt_get_mem_block_num(static_cast<rmt_channel_t>(ch), &usedBlocks);
      if (err != ESP_OK) {
        return Status(Err::HARDWARE_FAULT, err, "rmt_get_mem_block_num failed");
      }
      if (ch < config.rmtChannel + blocks && config.rmtChannel < ch + usedBlocks) {
        return Status(Err::RESOURCE_BUSY, ch, "RMT memory is already in use");
      }
    }

    rmt_config_t rmt_cfg = RMT_DEFAULT_CONFIG_TX(gpio, _channel);
    // Leave flags at 0 so the channel stays on the 80 MHz APB clock: the
    // DFS-aware source is XTAL on S3 and REF_TICK (1 MHz) on S2, either of
    // which would break the bit timings below.
    rmt_cfg.clk_div = kRmtClkDiv;
    rmt_cfg.mem_block_num = static_cast<uint8_t>(blocks);
    rmt_cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    rmt_cfg.tx_config.idle_output_en = true;
    rmt_cfg.tx_config.carrier_en = false;

    err = rmt_config(&rmt_cfg);
    if (err != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, err, "rmt_config failed");
    }
    // rmt_config() has now routed the pad to the RMT output signal, so record
    // it before anything else can fail: end() has to release it either way.
    _gpio = gpio;

    err = rmt_driver_install(_channel, 0, 0);
    if (err != ESP_OK) {
      // Installation can fail after allocating its channel object. No other
      // owner existed at the preflight check above; release partial resources.
      if (err != ESP_ERR_INVALID_STATE) {
        (void)rmt_driver_uninstall(_channel);
      }
      return Status(Err::HARDWARE_FAULT, err, "rmt_driver_install failed");
    }

    _installed = true;
    _count = config.ledCount;
    return Ok();
  }

  void end() override {
    if (_installed) {
      // Best effort: blank the LEDs so they do not stay lit after shutdown.
      if (_count > 0 && rmt_wait_tx_done(_channel, pdMS_TO_TICKS(kCleanupWaitMs)) == ESP_OK) {
        RgbColor blank[kMaxLeds]{};
        // An all-zero frame is identical in every color order.
        const size_t itemCount = buildItems(blank, _count, ColorOrder::GRB);
        if (itemCount > 0) {
          if (rmt_write_items(_channel, _items, static_cast<int>(itemCount), false) == ESP_OK) {
            (void)rmt_wait_tx_done(_channel, pdMS_TO_TICKS(kCleanupWaitMs));
          }
        }
      }
      (void)rmt_tx_stop(_channel);
      rmt_driver_uninstall(_channel);
      _installed = false;
      _count = 0;
    }

    // Uninstall leaves the pad routed to the now-dead RMT output signal, and a
    // failed begin() can leave it routed to a channel that was never installed.
    // Detach it either way and hold the data line low, the WS2812 idle state.
    if (_gpio != GPIO_NUM_NC) {
      esp_rom_gpio_connect_out_signal(_gpio, SIG_GPIO_OUT_IDX, false, false);
      gpio_set_direction(_gpio, GPIO_MODE_OUTPUT);
      gpio_set_level(_gpio, 0);
      _gpio = GPIO_NUM_NC;
    }
  }

  bool canShow() const override {
    if (!_installed) {
      return false;
    }
    // Polling with a zero wait is explicitly supported and does not log.
    return rmt_wait_tx_done(_channel, 0) != ESP_ERR_TIMEOUT;
  }

  Status show(const RgbColor* frame, uint8_t count, ColorOrder order) override {
    if (!_installed) {
      return Status(Err::NOT_INITIALIZED, 0, "Backend not initialized");
    }
    if (frame == nullptr) {
      return Status(Err::INVALID_CONFIG, 0, "frame must not be null");
    }
    if (count == 0 || count > _count) {
      return Status(Err::INVALID_CONFIG, count, "count out of range");
    }

    // The driver transmits straight out of _items, so it must not be rebuilt
    // while a previous frame is still on the wire.
    const esp_err_t waitErr = rmt_wait_tx_done(_channel, 0);
    if (waitErr == ESP_ERR_TIMEOUT) {
      return Status(Err::RESOURCE_BUSY, 0, "rmt busy");
    }
    if (waitErr != ESP_OK) {
      return Status(Err::HARDWARE_FAULT, waitErr, "rmt_wait_tx_done failed");
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
  // 80 MHz APB / 2 = 40 MHz, so one RMT tick is 25 ns.
  static constexpr uint8_t kRmtClkDiv = 2;
  static constexpr uint16_t kTicksPerUs = 40;
  // Latch gap, split over both halves of one item. A duration field is 15 bits
  // (max 32767 ticks = 819 us) and a zero duration would end the transmission.
  static constexpr uint16_t kResetTicksHalf = static_cast<uint16_t>(kTicksPerUs * (kResetUs / 2));
  static constexpr uint8_t kMaxLeds = ::StatusLed::StatusLed::kMaxLedCount;
  static constexpr uint16_t kMaxItems = (kMaxLeds * kBitsPerPixel) + 1;
  static constexpr uint8_t kMaxTxChannel = 3;  // channels 0..3 are TX on S2/S3
  static constexpr uint32_t kCleanupWaitMs = 50;

  /// @brief Encode a frame into RMT items. Returns 0 on failure.
  size_t buildItems(const RgbColor* frame, uint8_t count, ColorOrder order) {
    if (frame == nullptr || count == 0 || count > kMaxLeds) {
      return 0;
    }

    size_t idx = 0;
    for (uint8_t i = 0; i < count; ++i) {
      uint8_t wire[kBytesPerPixel];
      writePixelBytes(frame[i], order, wire);
      for (uint8_t b = 0; b < kBytesPerPixel; ++b) {
        if (!encodeByte(wire[b], idx)) {
          return 0;
        }
      }
    }

    if (idx >= kMaxItems) {
      return 0;
    }

    rmt_item32_t& reset = _items[idx++];
    reset.level0 = 0;
    reset.duration0 = kResetTicksHalf;
    reset.level1 = 0;
    reset.duration1 = kResetTicksHalf;

    return idx;
  }

  bool encodeByte(uint8_t value, size_t& idx) {
    for (int bit = 7; bit >= 0; --bit) {
      if (idx >= kMaxItems) {
        return false;
      }
      const bool isOne = ((value >> bit) & 0x1) != 0;
      rmt_item32_t& item = _items[idx++];
      item.level0 = 1;
      item.duration0 = isOne ? kRmtT1H : kRmtT0H;
      item.level1 = 0;
      item.duration1 = isOne ? kRmtT1L : kRmtT0L;
    }
    return true;
  }

  rmt_item32_t _items[kMaxItems]{};
  rmt_channel_t _channel = RMT_CHANNEL_0;
  gpio_num_t _gpio = GPIO_NUM_NC;
  bool _installed = false;
  uint8_t _count = 0;
};

}  // namespace

BackendBase* createBackend() {
  void* storage = heap_caps_malloc(sizeof(BackendIdfWs2812), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return storage == nullptr ? nullptr : new (storage) BackendIdfWs2812();
}

void destroyBackend(BackendBase* backend) {
  if (backend != nullptr) {
    auto* self = static_cast<BackendIdfWs2812*>(backend);
    self->~BackendIdfWs2812();
    heap_caps_free(self);
  }
}

}  // namespace StatusLed

#endif  // STATUSLED_BACKEND_IDF_WS2812
