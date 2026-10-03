#include "i2s_audio.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include <hal/dma_types.h>
#include "esp_rom_sys.h"
#include "esp_timer.h"

namespace esphome::i2s_audio {

static const char *const TAG = "i2s_audio";

// The shared DMA ring is fixed rather than taken from whichever side allocates first, so the speaker keeps its
// usual stall tolerance even when the microphone starts first. Matches the standard speaker's 5 x 10 ms ring.
static constexpr uint32_t FULL_DUPLEX_DMA_BUFFER_DURATION_MS = 10;
static constexpr uint32_t FULL_DUPLEX_DMA_BUFFERS_COUNT = 5;
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
static constexpr uint32_t FULL_DUPLEX_DMA_BUFFER_MAX_SIZE = DMA_DESCRIPTOR_BUFFER_MAX_SIZE_64B_ALIGNED;
#else
static constexpr uint32_t FULL_DUPLEX_DMA_BUFFER_MAX_SIZE = DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED;
#endif
// Long enough for an ISR that fired on_sent before a queue reset to also release its buffer
static constexpr uint32_t FULL_DUPLEX_ISR_SETTLE_US = 50;

static uint32_t effective_slot_bit_width(const i2s_std_slot_config_t &slot_cfg) {
  if (slot_cfg.slot_bit_width == I2S_SLOT_BIT_WIDTH_AUTO) {
    return static_cast<uint32_t>(slot_cfg.data_bit_width);
  }
  return static_cast<uint32_t>(slot_cfg.slot_bit_width);
}

esp_err_t I2SAudioComponent::allocate_full_duplex_channels_(const i2s_chan_config_t &chan_cfg,
                                                            const i2s_std_config_t &std_cfg) {
  if (this->rx_handle_ != nullptr && this->tx_handle_ != nullptr) {
    return ESP_OK;
  }
  if (!this->full_duplex_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (this->din_pin_ == I2S_GPIO_UNUSED || this->dout_pin_ == I2S_GPIO_UNUSED) {
    ESP_LOGE(TAG, "Full duplex requires both DIN and DOUT pins on the same I2S bus");
    return ESP_ERR_INVALID_ARG;
  }

  // Both channels share this config. If the microphone allocates first, its config has auto clear off, and
  // the DAC would then loop the last buffer whenever the speaker stops writing
  i2s_chan_config_t duplex_cfg = chan_cfg;
  duplex_cfg.auto_clear = true;
  const uint32_t frame_bytes = (static_cast<uint32_t>(std_cfg.slot_cfg.data_bit_width) / 8) *
                               (std_cfg.slot_cfg.slot_mode == I2S_SLOT_MODE_STEREO ? 2 : 1);
  duplex_cfg.dma_desc_num = FULL_DUPLEX_DMA_BUFFERS_COUNT;
  duplex_cfg.dma_frame_num = std::min(std_cfg.clk_cfg.sample_rate_hz * FULL_DUPLEX_DMA_BUFFER_DURATION_MS / 1000,
                                      FULL_DUPLEX_DMA_BUFFER_MAX_SIZE / frame_bytes);

  esp_err_t err = i2s_new_channel(&duplex_cfg, &this->tx_handle_, &this->rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Full duplex channel allocation failed: %s", esp_err_to_name(err));
    this->tx_handle_ = nullptr;
    this->rx_handle_ = nullptr;
    this->full_duplex_dma_desc_num_ = 0;
  } else {
    this->full_duplex_dma_desc_num_ = duplex_cfg.dma_desc_num;
  }
  return err;
}

esp_err_t I2SAudioComponent::initialize_full_duplex_channels_(const i2s_std_config_t &std_cfg) {
  if (this->tx_handle_ == nullptr || this->rx_handle_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  if (this->tx_channel_initialized_) {
    // Channels keep the first user's clock and slot layout; a different format would play at the wrong speed
    if (std_cfg.clk_cfg.sample_rate_hz != this->full_duplex_sample_rate_ ||
        static_cast<uint32_t>(std_cfg.slot_cfg.data_bit_width) != this->full_duplex_data_bit_width_ ||
        effective_slot_bit_width(std_cfg.slot_cfg) != this->full_duplex_slot_bit_width_ ||
        std_cfg.slot_cfg.slot_mode != this->full_duplex_slot_mode_) {
      ESP_LOGE(TAG,
               "Full duplex format mismatch: bus runs %" PRIu32 " Hz, %" PRIu32 "-bit data in %" PRIu32
               "-bit %s slots, requested %" PRIu32 " Hz, %" PRIu32 "-bit data in %" PRIu32 "-bit %s slots",
               this->full_duplex_sample_rate_, this->full_duplex_data_bit_width_, this->full_duplex_slot_bit_width_,
               this->full_duplex_slot_mode_ == I2S_SLOT_MODE_STEREO ? "stereo" : "mono",
               std_cfg.clk_cfg.sample_rate_hz, static_cast<uint32_t>(std_cfg.slot_cfg.data_bit_width),
               effective_slot_bit_width(std_cfg.slot_cfg),
               std_cfg.slot_cfg.slot_mode == I2S_SLOT_MODE_STEREO ? "stereo" : "mono");
      return ESP_ERR_INVALID_ARG;
    }
  }

  // Each user's pin config only names its own data pin, but both channels are initialized here
  i2s_std_config_t duplex_cfg = std_cfg;
  duplex_cfg.gpio_cfg = this->get_full_duplex_pin_config();

  if (!this->tx_channel_initialized_) {
    esp_err_t err = i2s_channel_init_std_mode(this->tx_handle_, &duplex_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize full duplex TX channel: %s", esp_err_to_name(err));
      return err;
    }
    this->tx_channel_initialized_ = true;
    this->full_duplex_sample_rate_ = std_cfg.clk_cfg.sample_rate_hz;
    this->full_duplex_data_bit_width_ = static_cast<uint32_t>(std_cfg.slot_cfg.data_bit_width);
    this->full_duplex_slot_bit_width_ = effective_slot_bit_width(std_cfg.slot_cfg);
    this->full_duplex_slot_mode_ = std_cfg.slot_cfg.slot_mode;
  }

  if (!this->rx_channel_initialized_) {
    esp_err_t err = i2s_channel_init_std_mode(this->rx_handle_, &duplex_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize full duplex RX channel: %s", esp_err_to_name(err));
      return err;
    }
    this->rx_channel_initialized_ = true;
  }

  return ESP_OK;
}

esp_err_t I2SAudioComponent::setup_full_duplex_rx_channel(const i2s_chan_config_t &chan_cfg,
                                                          const i2s_std_config_t &std_cfg,
                                                          i2s_chan_handle_t *rx_handle) {
  LockGuard guard(this->full_duplex_lock_);
  esp_err_t err = this->allocate_full_duplex_channels_(chan_cfg, std_cfg);
  if (err != ESP_OK) {
    return err;
  }
  err = this->initialize_full_duplex_channels_(std_cfg);
  if (err == ESP_OK) {
    *rx_handle = this->rx_handle_;
  }
  return err;
}

esp_err_t I2SAudioComponent::setup_full_duplex_tx_channel(const i2s_chan_config_t &chan_cfg,
                                                          const i2s_std_config_t &std_cfg,
                                                          i2s_chan_handle_t *tx_handle) {
  LockGuard guard(this->full_duplex_lock_);
  esp_err_t err = this->allocate_full_duplex_channels_(chan_cfg, std_cfg);
  if (err != ESP_OK) {
    return err;
  }
  err = this->initialize_full_duplex_channels_(std_cfg);
  if (err == ESP_OK) {
    *tx_handle = this->tx_handle_;
  }
  return err;
}

size_t I2SAudioComponent::get_full_duplex_dma_buffer_bytes_() {
  i2s_chan_info_t chan_info;
  if (this->full_duplex_dma_desc_num_ == 0 || i2s_channel_get_info(this->tx_handle_, &chan_info) != ESP_OK) {
    return 0;
  }
  return chan_info.total_dma_buf_size / this->full_duplex_dma_desc_num_;
}

// Caller holds full_duplex_lock_
esp_err_t I2SAudioComponent::start_full_duplex_tx_(const uint8_t *silence, size_t buffer_bytes) {
  esp_err_t err;
  if (!this->full_duplex_tx_callback_registered_) {
    // Must be registered before the channel is enabled
    const i2s_event_callbacks_t callbacks = {.on_sent = I2SAudioComponent::full_duplex_on_sent_cb};
    err = i2s_channel_register_event_callback(this->tx_handle_, &callbacks, this);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Full duplex TX callback registration failed: %s", esp_err_to_name(err));
      return err;
    }
    this->full_duplex_tx_callback_registered_ = true;
  }

  // Preload every descriptor so the first on_sent events pair with known silence
  for (size_t i = 0; i < this->full_duplex_dma_desc_num_; i++) {
    size_t bytes_loaded = 0;
    err = i2s_channel_preload_data(this->tx_handle_, silence, buffer_bytes, &bytes_loaded);
    if (err != ESP_OK || bytes_loaded != buffer_bytes) {
      ESP_LOGE(TAG, "Full duplex TX silence preload failed: %s (%u/%u bytes)", esp_err_to_name(err),
               (unsigned) bytes_loaded, (unsigned) buffer_bytes);
      return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }
  }

  err = i2s_channel_enable(this->tx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Full duplex TX clock enable failed: %s", esp_err_to_name(err));
    return err;
  }
  this->full_duplex_tx_enabled_ = true;
  ESP_LOGD(TAG, "Full duplex TX clock is running");
  return ESP_OK;
}

esp_err_t I2SAudioComponent::ensure_full_duplex_tx_running() {
  LockGuard guard(this->full_duplex_lock_);
  if (!this->full_duplex_ || this->tx_handle_ == nullptr || !this->tx_channel_initialized_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (this->full_duplex_tx_enabled_) {
    return ESP_OK;
  }

  const size_t buffer_bytes = this->get_full_duplex_dma_buffer_bytes_();
  if (buffer_bytes == 0) {
    ESP_LOGE(TAG, "Full duplex TX clock start failed: no DMA buffer size available");
    return ESP_ERR_INVALID_SIZE;
  }

  RAMAllocator<uint8_t> allocator;
  uint8_t *silence = allocator.allocate(buffer_bytes);
  if (silence == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  memset(silence, 0, buffer_bytes);
  esp_err_t err = this->start_full_duplex_tx_(silence, buffer_bytes);
  allocator.deallocate(silence, buffer_bytes);
  return err;
}

esp_err_t I2SAudioComponent::attach_full_duplex_speaker(QueueHandle_t event_queue, QueueHandle_t records_queue,
                                                        EventGroupHandle_t event_group, EventBits_t overflow_bits,
                                                        const uint8_t *silence, size_t buffer_bytes) {
  // Every descriptor finishes once before the speaker's first write reaches the wire, in both paths below
  auto seed_records = [&]() -> bool {
    const uint32_t zero_real_frames = 0;
    for (size_t i = 0; i < this->full_duplex_dma_desc_num_; i++) {
      if (xQueueSend(records_queue, &zero_real_frames, 0) != pdTRUE) {
        return false;
      }
    }
    return true;
  };

  {
    LockGuard guard(this->full_duplex_lock_);
    if (!this->full_duplex_ || this->tx_handle_ == nullptr || !this->tx_channel_initialized_) {
      return ESP_ERR_INVALID_STATE;
    }

    this->full_duplex_tx_event_group_ = event_group;
    this->full_duplex_tx_overflow_bits_ = overflow_bits;

    if (!this->full_duplex_tx_enabled_) {
      // Fresh start, identical to a simplex speaker: preload silence, then enable
      if (!seed_records()) {
        return ESP_ERR_NO_MEM;
      }
      this->full_duplex_tx_event_queue_.store(event_queue, std::memory_order_release);
      esp_err_t err = this->start_full_duplex_tx_(silence, buffer_bytes);
      if (err != ESP_OK) {
        this->full_duplex_tx_event_queue_.store(nullptr, std::memory_order_release);
      }
      return err;
    }
  }

  // TX is already clocking the microphone, so its DMA ring holds buffers it has released but nobody refilled.
  // A write would land in one of those and play early, skewing timestamps by up to a whole ring. Claim them all
  // with silence so the next write waits for the buffer on the wire, the same position as a fresh preload.
  //
  // First let one full ring play out untouched. That fills the driver's release queue, and a full queue makes
  // its next write start a fresh buffer, discarding any partial write position a previous session left behind.
  // The lock is released meanwhile so microphone start/stop on the main loop isn't stalled; that's safe because
  // TX is never disabled once enabled and only this speaker task writes to it.
  vTaskDelay(pdMS_TO_TICKS(this->full_duplex_dma_desc_num_ * FULL_DUPLEX_DMA_BUFFER_DURATION_MS + 1));
  LockGuard guard(this->full_duplex_lock_);

  // Events that arrive meanwhile are discarded; the loop exits once nothing was released since the last reset
  this->full_duplex_tx_event_queue_.store(event_queue, std::memory_order_release);
  size_t bytes_written = 0;
  do {
    while (i2s_channel_write(this->tx_handle_, silence, buffer_bytes, &bytes_written, 0) == ESP_OK) {
    }
    xQueueReset(event_queue);
    esp_rom_delay_us(FULL_DUPLEX_ISR_SETTLE_US);
  } while (i2s_channel_write(this->tx_handle_, silence, buffer_bytes, &bytes_written, 0) == ESP_OK);

  if (!seed_records()) {
    this->full_duplex_tx_event_queue_.store(nullptr, std::memory_order_release);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void I2SAudioComponent::detach_full_duplex_tx_event_queue(QueueHandle_t queue) {
  LockGuard guard(this->full_duplex_lock_);
  QueueHandle_t expected = queue;
  this->full_duplex_tx_event_queue_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

bool IRAM_ATTR I2SAudioComponent::full_duplex_on_sent_cb(i2s_chan_handle_t handle, i2s_event_data_t *event,
                                                         void *user_ctx) {
  (void) handle;
  (void) event;
  auto *component = static_cast<I2SAudioComponent *>(user_ctx);
  QueueHandle_t queue = component->full_duplex_tx_event_queue_.load(std::memory_order_acquire);
  if (queue == nullptr) {
    return false;
  }

  int64_t now = esp_timer_get_time();
  BaseType_t need_yield1 = pdFALSE;
  BaseType_t need_yield2 = pdFALSE;
  BaseType_t need_yield3 = pdFALSE;

  // Mirrors I2SAudioSpeakerBase::i2s_on_sent_cb: a dropped event desyncs the speaker's records, so it restarts
  if (xQueueIsQueueFullFromISR(queue)) {
    int64_t dummy;
    xQueueReceiveFromISR(queue, &dummy, &need_yield1);
    if (component->full_duplex_tx_event_group_ != nullptr && component->full_duplex_tx_overflow_bits_ != 0) {
      xEventGroupSetBitsFromISR(component->full_duplex_tx_event_group_, component->full_duplex_tx_overflow_bits_,
                                &need_yield2);
    }
  }

  xQueueSendToBackFromISR(queue, &now, &need_yield3);
  return need_yield1 | need_yield2 | need_yield3;
}

void I2SAudioComponent::release_full_duplex_rx_channel(i2s_chan_handle_t rx_handle) {
  LockGuard guard(this->full_duplex_lock_);
  if (rx_handle == this->rx_handle_ && this->full_duplex_rx_enabled_) {
    i2s_channel_disable(this->rx_handle_);
    this->full_duplex_rx_enabled_ = false;
  }
}

void I2SAudioComponent::release_full_duplex_tx_channel(i2s_chan_handle_t tx_handle) {
  (void) tx_handle;
  // TX stays enabled: it clocks the external ADC, and stopping it would stall RX until playback resumes
}

}  // namespace esphome::i2s_audio

#endif  // USE_ESP32
