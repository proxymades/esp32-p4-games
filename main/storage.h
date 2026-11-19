#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t storage_load_rom(const char *path, uint8_t **rom, size_t *length);
