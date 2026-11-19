#include "storage.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "gwenesis_port.h"
#include "mbedtls/sha256.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

static const char *TAG = "storage";

esp_err_t storage_load_rom(const char *path, uint8_t **rom, size_t *length)
{
    if (path == NULL || rom == NULL || length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *rom = NULL;
    *length = 0;
    sd_pwr_ctrl_handle_t power = NULL;
    const sd_pwr_ctrl_ldo_config_t supply = {.ldo_chan_id = 4};
    esp_err_t result = sd_pwr_ctrl_new_on_chip_ldo(&supply, &power);
    if (result != ESP_OK) {
        return result;
    }
    result = sd_pwr_ctrl_set_io_voltage(power, 3300);
    if (result != ESP_OK) {
        sd_pwr_ctrl_del_on_chip_ldo(power);
        return result;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.pwr_ctrl_handle = power;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    slot.d1 = BOARD_SD_D1;
    slot.d2 = BOARD_SD_D2;
    slot.d3 = BOARD_SD_D3;
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    gpio_set_level(BOARD_SD_POWER, 0);
    result = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mount, &card);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "SD_NOT_READY %s; no formatting or retries", esp_err_to_name(result));
        gpio_set_level(BOARD_SD_POWER, 1);
        sd_pwr_ctrl_del_on_chip_ldo(power);
        return result;
    }
    FILE *file = fopen(path, "rb");
    uint8_t *data = NULL;
    if (file == NULL) {
        result = ESP_ERR_NOT_FOUND;
        ESP_LOGE(TAG, "ROM_NOT_FOUND %s", path);
        goto done;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        result = ESP_FAIL;
        goto done;
    }
    const long size = ftell(file);
    if (size < 512 || size > 8 * 1024 * 1024 || (size & 1) != 0) {
        result = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    rewind(file);
    data = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size || !gw_rom_valid(data, (size_t)size)) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    unsigned char digest[32];
    if (mbedtls_sha256(data, (size_t)size, digest, 0) != 0) {
        result = ESP_FAIL;
        goto done;
    }
    char hex[65];
    for (unsigned i = 0; i < 32; ++i) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    ESP_LOGI(TAG, "ROM_LOADED bytes=%ld sha256=%s", size, hex);
    *rom = data;
    *length = (size_t)size;
    data = NULL;
    result = ESP_OK;
done:
    if (file != NULL) {
        fclose(file);
    }
    heap_caps_free(data);
    esp_vfs_fat_sdcard_unmount("/sd", card);
    gpio_set_level(BOARD_SD_POWER, 1);
    sd_pwr_ctrl_del_on_chip_ldo(power);
    return result;
}
