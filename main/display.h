#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t display_init(void);
uint16_t *display_acquire(void);
esp_err_t display_submit(uint16_t *pixels, unsigned frame, unsigned height);
void display_stats(void);
esp_err_t display_test_pattern(void);
