#pragma once
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
static inline uint32_t gw_cycle_now(void) { return esp_cpu_get_cycle_count(); }
#else
static inline uint32_t gw_cycle_now(void) { return 0; }
#endif
extern uint32_t gw_synth_cycles;
