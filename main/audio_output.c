#include "audio_output.h"
#include <stdatomic.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define AUDIO_BLOCKS 4
#define AUDIO_SAMPLES 1024
#define AUDIO_PREROLL_BLOCKS 3
typedef struct {
    size_t count;
    int16_t mono[AUDIO_SAMPLES];
} audio_block_t;

static const char *TAG = "audio";
static QueueHandle_t s_free;
static QueueHandle_t s_ready;
static SemaphoreHandle_t s_finished;
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t s_tx;
static TaskHandle_t s_worker;
static bool s_released;
static atomic_uint s_dma_events;
static atomic_uint s_dma_queue_overflows;
static atomic_uint s_starvations;
static atomic_uint s_write_errors;
static atomic_uint s_samples;
static int16_t s_stereo[AUDIO_SAMPLES * 2];

static bool IRAM_ATTR sent_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *context)
{
    (void)handle;
    (void)event;
    (void)context;
    atomic_fetch_add_explicit(&s_dma_events, 1, memory_order_relaxed);
    return false;
}

static bool IRAM_ATTR overflow_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *context)
{
    (void)handle;
    (void)event;
    (void)context;
    atomic_fetch_add_explicit(&s_dma_queue_overflows, 1, memory_order_relaxed);
    return false;
}

static void audio_worker(void *arg)
{
    (void)arg;
    /* About 50 ms of PCM absorbs raster/logging jitter before unmuting. */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (i2s_channel_enable(s_tx) != ESP_OK) {
        atomic_fetch_add(&s_write_errors, 1);
    }
    bool started = false;
    for (;;) {
        audio_block_t *block = NULL;
        if (xQueueReceive(s_ready, &block, started ? pdMS_TO_TICKS(45) : portMAX_DELAY) != pdTRUE) {
            atomic_fetch_add(&s_starvations, 1);
            /* auto_clear_after_cb supplies silence after genuine DMA drain.
               Do not enqueue extra samples while the previous DMA data plays. */
            continue;
        }
        if (block == NULL) {
            break;
        }
        for (size_t i = 0; i < block->count; ++i) {
            s_stereo[i * 2] = block->mono[i];
            s_stereo[i * 2 + 1] = block->mono[i];
        }
        if (esp_codec_dev_write(s_codec, s_stereo, block->count * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            atomic_fetch_add(&s_write_errors, 1);
        } else {
            atomic_fetch_add(&s_samples, (unsigned)block->count);
            if (!started) {
                if (esp_codec_dev_set_out_mute(s_codec, false) != ESP_CODEC_DEV_OK) {
                    atomic_fetch_add(&s_write_errors, 1);
                }
                started = true;
            }
        }
        xQueueSend(s_free, &block, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "AUDIO_DRAIN dma_queue_overflows=%u pcm_starvations=%u write_errors=%u",
             atomic_load(&s_dma_queue_overflows), atomic_load(&s_starvations),
             atomic_load(&s_write_errors));
    /* 8 * 256 frames / 48 kHz = 42.67 ms maximum outstanding DMA. */
    vTaskDelay(pdMS_TO_TICKS(45));
    esp_codec_dev_set_out_mute(s_codec, true);
    gpio_set_level(BOARD_AMP_ENABLE, 0);
    esp_codec_dev_close(s_codec);
    xSemaphoreGive(s_finished);
    vTaskDelete(NULL);
}

esp_err_t audio_output_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 256;
    chan.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, NULL), TAG, "I2S channel");
    const i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT, .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &standard), TAG, "I2S standard");
    const i2s_event_callbacks_t callbacks = {
        .on_sent = sent_callback, .on_send_q_ovf = overflow_callback,
    };
    ESP_RETURN_ON_ERROR(i2s_channel_register_event_callback(s_tx, &callbacks, NULL), TAG, "I2S callbacks");

    /* esp_codec_dev disables the supplied channel before reconfiguration. */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "initial I2S enable");
    audio_codec_i2s_cfg_t data_cfg = {.port = I2S_NUM_0, .tx_handle = s_tx};
    audio_codec_i2c_cfg_t control_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR, /* Codec library expects 8-bit 0x30, bus address is 0x18. */
        .bus_handle = board_i2c(),
    };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_cfg);
    const audio_codec_ctrl_if_t *control = audio_codec_new_i2c_ctrl(&control_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data && control && gpio, ESP_ERR_NO_MEM, TAG, "codec interfaces");
    es8311_codec_cfg_t cfg = {
        .ctrl_if = control, .gpio_if = gpio, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BOARD_AMP_ENABLE, .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
    };
    const audio_codec_if_t *codec = es8311_codec_new(&cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_ERR_NO_MEM, TAG, "ES8311 driver");
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec, .data_if = data, .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_NO_MEM, TAG, "codec device");
    esp_codec_dev_sample_info_t format = {.sample_rate = 48000, .channel = 2, .bits_per_sample = 16};
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &format) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_mute(s_codec, true) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "mute");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(s_codec, CONFIG_P4GAME_AUDIO_VOLUME) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "volume");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx), TAG, "pause I2S before preroll");
    s_free = xQueueCreate(AUDIO_BLOCKS, sizeof(audio_block_t *));
    s_ready = xQueueCreate(AUDIO_BLOCKS, sizeof(audio_block_t *));
    s_finished = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_free && s_ready && s_finished, ESP_ERR_NO_MEM, TAG, "audio queues");
    for (unsigned i = 0; i < AUDIO_BLOCKS; ++i) {
        audio_block_t *block = heap_caps_calloc(1, sizeof(*block), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(block, ESP_ERR_NO_MEM, TAG, "PCM blocks");
        xQueueSend(s_free, &block, portMAX_DELAY);
    }
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(audio_worker, "audio_out", 4096, NULL, 6, &s_worker, 1) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "audio task");
    ESP_LOGI(TAG, "AUDIO_CONFIGURED ES8311 48kHz/16-bit mono via stereo I2S DMA volume=%d",
             CONFIG_P4GAME_AUDIO_VOLUME);
    return ESP_OK;
}

static void queue_pcm(audio_block_t *block)
{
    xQueueSend(s_ready, &block, portMAX_DELAY);
    if (!s_released && uxQueueMessagesWaiting(s_ready) >= AUDIO_PREROLL_BLOCKS) {
        s_released = true;
        xTaskNotifyGive(s_worker);
    }
}

esp_err_t audio_output_frame(audio_resample_t *resampler, const gw_frame_t *frame)
{
    audio_block_t *block = NULL;
    if (xQueueReceive(s_free, &block, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    block->count = audio_resample(resampler, frame->fm, frame->psg, frame->audio_samples,
                                  frame->master_clock_hz, block->mono, AUDIO_SAMPLES);
    if (block->count == SIZE_MAX) {
        xQueueSend(s_free, &block, portMAX_DELAY);
        return ESP_ERR_INVALID_SIZE;
    }
    queue_pcm(block);
    return ESP_OK;
}

esp_err_t audio_output_tone(void)
{
    unsigned phase = 0;
    for (unsigned fragment = 0; fragment < 12; ++fragment) {
        audio_block_t *block = NULL;
        if (xQueueReceive(s_free, &block, pdMS_TO_TICKS(250)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        block->count = 1000;
        for (size_t i = 0; i < block->count; ++i) {
            phase = (phase + 440) % 48000;
            const int value = phase < 24000 ? (int)phase : 48000 - (int)phase;
            block->mono[i] = (int16_t)((value - 12000) / 4);
        }
        queue_pcm(block);
    }
    return ESP_OK;
}

esp_err_t audio_output_finish(void)
{
    audio_block_t *sentinel = NULL;
    xQueueSend(s_ready, &sentinel, portMAX_DELAY);
    if (!s_released) {
        s_released = true;
        xTaskNotifyGive(s_worker);
    }
    if (xSemaphoreTake(s_finished, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return atomic_load(&s_write_errors) == 0 ? ESP_OK : ESP_FAIL;
}

void audio_output_stats(void)
{
    ESP_LOGI(TAG, "AUDIO_STATS pcm_samples=%u dma_events=%u dma_queue_overflows=%u pcm_starvations=%u write_errors=%u",
             atomic_load(&s_samples), atomic_load(&s_dma_events), atomic_load(&s_dma_queue_overflows),
             atomic_load(&s_starvations), atomic_load(&s_write_errors));
}
