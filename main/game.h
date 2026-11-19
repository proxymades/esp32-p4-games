#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t game_run(uint8_t *rom, size_t length, bool diagnostic, unsigned test_frames);
