#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "board.h"
#include "diagnostics.h"
#include "audio_output.h"
#include "buttons.h"
#include "display.h"
#include "game.h"
#include "storage.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "games";

static void idle(void)
{
    for (;;) {
        ESP_LOGI(TAG, "ALIVE uptime_s=%" PRId64 " internal_free=%u psram_free=%u",
                 esp_timer_get_time() / 1000000,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#if CONFIG_P4GAME_PLAY || CONFIG_P4GAME_ROM_TEST
static void play_task(void *arg)
{
    (void)arg;
    uint8_t *rom = NULL;
    size_t length = 0;
#if CONFIG_P4GAME_ROM_TEST
    extern const uint8_t test_rom_start[] asm("_binary_test_rom_start");
    extern const uint8_t test_rom_end[] asm("_binary_test_rom_end");
    length = (size_t)(test_rom_end - test_rom_start);
    rom = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rom == NULL) {
        ESP_LOGE(TAG, "Test ROM allocation failed");
        idle();
    }
    memcpy(rom, test_rom_start, length);
    ESP_LOGI(TAG, "Private embedded ROM test, SPI active, no SD; Start stimulus at frame 1900");
    const esp_err_t result = game_run(rom, length, true, CONFIG_P4GAME_TEST_FRAMES);
#else
    esp_err_t result = storage_load_rom(CONFIG_P4GAME_ROM_PATH, &rom, &length);
    ESP_ERROR_CHECK(board_verify_pinmux());
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Insert microSD with %s and reset. %s", CONFIG_P4GAME_ROM_PATH, esp_err_to_name(result));
        audio_output_finish();
        idle();
    }
    result = game_run(rom, length, false, 0);
#endif
    heap_caps_free(rom);
    ESP_LOGI(TAG, "Game task ended: %s", esp_err_to_name(result));
    idle();
}

#endif

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "P4-NANO Games chip_revision=%u.%u cores=%u reset=%d",
             chip.revision / 100, chip.revision % 100, chip.cores, esp_reset_reason());
    ESP_ERROR_CHECK(board_init());
    const esp_err_t codec = board_probe_codec();
#if CONFIG_P4GAME_BOARD_CHECK
    const esp_err_t memory = diagnostics_psram();
    ESP_LOGI(TAG, "BOARD_CHECK %s memory=%s codec=%s no_sd_required=1",
             memory == ESP_OK && codec == ESP_OK ? "PASS" : "FAIL",
             esp_err_to_name(memory), esp_err_to_name(codec));
    idle();
#else
    if (codec != ESP_OK) {
        idle();
    }
    ESP_ERROR_CHECK(buttons_start());
    ESP_ERROR_CHECK(audio_output_init());
#if CONFIG_P4GAME_PERIPHERAL_TEST
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(board_verify_pinmux());
    ESP_ERROR_CHECK(display_test_pattern());
    ESP_ERROR_CHECK(audio_output_tone());
    ESP_ERROR_CHECK(audio_output_finish());
    uint8_t *rom = NULL;
    size_t length = 0;
    const esp_err_t sd = storage_load_rom(CONFIG_P4GAME_ROM_PATH, &rom, &length);
    ESP_ERROR_CHECK(board_verify_pinmux());
    ESP_LOGI(TAG, "PERIPHERAL_TEST sd=%s rom_bytes=%u; inspect panel/buttons/speaker physically",
             esp_err_to_name(sd), (unsigned)length);
    heap_caps_free(rom);
    audio_output_stats();
    display_stats();
    idle();
#else
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(board_verify_pinmux());
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(play_task, "emulation", 16 * 1024, NULL, 4, NULL, 0) == pdPASS ?
                    ESP_OK : ESP_ERR_NO_MEM);
#endif
#endif
}
