#include "audio_resample.h"

size_t audio_resample(audio_resample_t *state, const int16_t *fm, const int16_t *psg,
                      size_t count, uint32_t master_clock_hz, int16_t *output, size_t capacity)
{
    /* One synth sample spans 1009 master cycles. Keep the rational phase
       across frames, so both NTSC and PAL feed a fixed 48 kHz DAC. */
    const uint32_t step = 48000U * 1009U;
    if (state == NULL || fm == NULL || psg == NULL || output == NULL ||
        master_clock_hz < step) {
        return SIZE_MAX;
    }
    if (state->clock_hz != master_clock_hz) {
        *state = (audio_resample_t){.clock_hz = master_clock_hz};
    }
    size_t produced = 0;
    for (size_t i = 0; i < count; ++i) {
        const int16_t current = (int16_t)(((int32_t)fm[i] + psg[i]) / 2);
        state->phase += step;
        if (state->phase >= master_clock_hz) {
            state->phase -= master_clock_hz;
            if (produced == capacity) {
                return SIZE_MAX;
            }
            const int64_t delta = (int32_t)current - state->previous;
            output[produced++] = (int16_t)(state->previous +
                delta * (step - state->phase) / step);
        }
        state->previous = current;
    }
    return produced;
}
