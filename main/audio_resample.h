#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t phase;
    uint32_t clock_hz;
    int16_t previous;
} audio_resample_t;

size_t audio_resample(audio_resample_t *state, const int16_t *fm, const int16_t *psg,
                      size_t count, uint32_t master_clock_hz, int16_t *output, size_t capacity);
