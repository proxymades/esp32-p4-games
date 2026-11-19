#pragma once
#include <stdint.h>
typedef struct {
    uint8_t candidate;
    uint8_t stable;
    uint32_t changed_us[8];
} button_filter_t;
uint8_t button_filter_update(button_filter_t *filter, uint8_t pressed, uint32_t now_us);
