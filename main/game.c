#include "game.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include "audio_output.h"
#include "audio_resample.h"
#include "buttons.h"
#include "checksum.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gwenesis_port.h"

static const char *TAG = "game";
static void *core_alloc(size_t bytes, bool external)
{
    const uint32_t caps = MALLOC_CAP_8BIT |
                          (external ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
    void *memory = heap_caps_malloc(bytes, caps);
    if (memory == NULL) {
        ESP_LOGE(TAG, "Allocation failed bytes=%u external=%d free=%u largest=%u",
                 (unsigned)bytes, external, (unsigned)heap_caps_get_free_size(caps),
                 (unsigned)heap_caps_get_largest_free_block(caps));
    }
    return memory;
}

esp_err_t game_run(uint8_t *rom, size_t length, bool diagnostic, unsigned test_frames)
{
    if (!gw_init(rom, length, core_alloc, heap_caps_free)) {
        ESP_LOGE(TAG, "Emulator initialization failed (ROM or memory)");
        return ESP_FAIL;
    }
    uint16_t *test_pixels = NULL;
    if (diagnostic) {
        test_pixels = heap_caps_malloc(GW_WIDTH * GW_HEIGHT * sizeof(uint16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (test_pixels == NULL) {
            gw_shutdown();
            return ESP_ERR_NO_MEM;
        }
    }
    audio_resample_t resampler = {0};
    uint64_t emulated_cycles = 0;
    const int64_t start = esp_timer_get_time();
    int64_t report_start = start;
    int64_t cpu_total = 0;
    uint64_t logic_total = 0, synth_total = 0, video_total = 0, rgb_total = 0;
    unsigned interval_frames = 0;
    unsigned frame_number = 0;
    unsigned late_frames = 0;
    unsigned no_framebuffer = 0;
    esp_err_t status = ESP_OK;
    ESP_LOGI(TAG, "EMU_START diagnostic=%d ROM=%u bytes accurate_audio=1",
             diagnostic, (unsigned)length);
    while (test_frames == 0 || frame_number < test_frames) {
        const bool checkpoint = diagnostic &&
            ((frame_number + 1) % 120 == 0 || frame_number + 1 == test_frames);
        uint16_t *display_pixels = display_acquire();
        uint16_t *pixels = display_pixels;
        if (pixels == NULL && checkpoint) {
            pixels = test_pixels;
        }
        if (pixels == NULL) {
            ++no_framebuffer;
        }
        uint8_t input = buttons_pressed();
        /* This is a diagnostic stimulus only. Normal play uses GPIO exclusively. */
        if (diagnostic && frame_number + 1 >= 1900 && frame_number + 1 < 1905) {
            input |= 0x80;
        }
        const int64_t cpu_start = esp_timer_get_time();
        gw_frame_t frame;
        if (!gw_frame(input, pixels, &frame)) {
            ESP_LOGE(TAG, "Emulation/audio buffer error at frame %u", frame_number + 1);
            status = ESP_FAIL;
            break;
        }
        cpu_total += esp_timer_get_time() - cpu_start;
        logic_total += frame.logic_cycles;
        synth_total += frame.synth_cycles;
        video_total += frame.video_cycles;
        rgb_total += frame.rgb_cycles;
        ++frame_number;
        ++interval_frames;
        emulated_cycles += frame.frame_cycles;
        if (checkpoint) {
            ESP_LOGI(TAG, "FRAME %u hash=%08" PRIx32 " pc=%06" PRIx32 " video=%ux%u synth=%u",
                     frame_number, game_hash32(pixels, GW_WIDTH * GW_HEIGHT * sizeof(uint16_t)),
                     frame.program_counter, frame.width, frame.height, frame.audio_samples);
            ESP_LOGI(TAG, "AUDIO_FRAME %u fm_hash=%08" PRIx32 " psg_hash=%08" PRIx32,
                     frame_number, game_hash32(frame.fm, frame.audio_samples * sizeof(*frame.fm)),
                     game_hash32(frame.psg, frame.audio_samples * sizeof(*frame.psg)));
        }
        if (display_pixels != NULL) {
            status = display_submit(display_pixels, frame_number, frame.height);
            if (status != ESP_OK) {
                break;
            }
        }
        status = audio_output_frame(&resampler, &frame);
        if (status != ESP_OK) {
            ESP_LOGE(TAG, "PCM output error: %s", esp_err_to_name(status));
            break;
        }
        const int64_t deadline = start + (int64_t)(emulated_cycles * 1000000ULL / frame.master_clock_hz);
        const int64_t now = esp_timer_get_time();
        if (now > deadline + 2000) {
            ++late_frames;
        }
        if (deadline > now + 1000) {
            vTaskDelay(pdMS_TO_TICKS((unsigned)((deadline - now) / 1000) - 1));
        }
        /* Always leave an idle slot for the watchdog, even under overload. */
        vTaskDelay(1);
        const int64_t report_now = esp_timer_get_time();
        if (report_now - report_start >= 2000000) {
            ESP_LOGI(TAG, "EMU_STATS frame=%u fps_x100=%u cpu_us=%u late=%u no_video_buffer=%u internal_free=%u stack_min_bytes=%u",
                     frame_number,
                     (unsigned)(interval_frames * 100000000LL / (report_now - report_start)),
                     (unsigned)(cpu_total / interval_frames), late_frames, no_framebuffer,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            ESP_LOGI(TAG, "PROFILE logic_us=%u synth_us=%u raster_us=%u rgb_us=%u",
                     (unsigned)(logic_total / interval_frames / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
                     (unsigned)(synth_total / interval_frames / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
                     (unsigned)((video_total - rgb_total) / interval_frames / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
                     (unsigned)(rgb_total / interval_frames / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ));
            audio_output_stats();
            display_stats();
            report_start = report_now;
            interval_frames = 0;
            cpu_total = 0;
            logic_total = 0; synth_total = 0; video_total = 0; rgb_total = 0;
        }
    }
    if (test_frames != 0) {
        const esp_err_t stop = audio_output_finish();
        if (stop != ESP_OK) {
            status = stop;
        }
        audio_output_stats();
        const bool heap_ok = heap_caps_check_integrity_all(true);
        ESP_LOGI(TAG, "HEAP_INTEGRITY %s", heap_ok ? "PASS" : "FAIL");
        if (!heap_ok) {
            status = ESP_FAIL;
        }
        ESP_LOGI(TAG, "ROM_TEST %s frames=%u elapsed_ms=%" PRId64,
                 status == ESP_OK ? "PASS" : "FAIL", frame_number,
                 (esp_timer_get_time() - start) / 1000);
    }
    heap_caps_free(test_pixels);
    gw_shutdown();
    return status;
}
