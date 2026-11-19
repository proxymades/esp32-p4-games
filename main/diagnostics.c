#include "diagnostics.h"
#include <inttypes.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "diagnostics";

static uint32_t test_pattern(size_t index, unsigned pass)
{
    if (pass == 0) {
        return 0;
    }
    if (pass == 1) {
        return UINT32_MAX;
    }
    const uint32_t address_pattern = (uint32_t)index * UINT32_C(0x9e3779b9);
    return pass == 2 ? address_pattern : ~address_pattern;
}

esp_err_t diagnostics_psram(void)
{
    const size_t detected = esp_psram_get_size();
    ESP_LOGI(TAG, "PSRAM detected=%u bytes", (unsigned)detected);
    if (detected != 32U * 1024U * 1024U) {
        ESP_LOGE(TAG, "PSRAM_SIZE FAIL expected 32 MiB");
        return ESP_FAIL;
    }
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (largest < 20U * 1024U * 1024U) {
        ESP_LOGE(TAG, "PSRAM test allocation too small");
        return ESP_ERR_NO_MEM;
    }
    const size_t bytes = (largest - 4096U) & ~(size_t)63;
    volatile uint32_t *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const size_t words = bytes / sizeof(uint32_t);
    const int64_t start = esp_timer_get_time();
    for (unsigned pass = 0; pass < 4; ++pass) {
        for (size_t i = 0; i < words; ++i) {
            memory[i] = test_pattern(i, pass);
            if ((i & 0x3ffffU) == 0) {
                vTaskDelay(1);
            }
        }
        for (size_t i = 0; i < words; ++i) {
            const uint32_t expected = test_pattern(i, pass);
            const uint32_t actual = memory[i];
            if (actual != expected) {
                ESP_LOGE(TAG, "PSRAM_TEST FAIL pass=%u offset=%u actual=%08" PRIx32 " expected=%08" PRIx32,
                         pass, (unsigned)(i * 4), actual, expected);
                heap_caps_free((void *)memory);
                return ESP_FAIL;
            }
            if ((i & 0x3ffffU) == 0) {
                vTaskDelay(1);
            }
        }
        ESP_LOGI(TAG, "PSRAM pass=%u bytes=%u OK", pass, (unsigned)bytes);
    }
    heap_caps_free((void *)memory);
    ESP_LOGI(TAG, "PSRAM_TEST PASS tested=%u passes=4 elapsed_ms=%" PRId64,
             (unsigned)bytes, (esp_timer_get_time() - start) / 1000);
    return ESP_OK;
}
