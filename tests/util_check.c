#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_resample.h"
#include "button_filter.h"
#include "checksum.h"

static void check_buttons(void)
{
    button_filter_t f = {0};
    assert(button_filter_update(&f, 0, 0) == 0);
    assert(button_filter_update(&f, 1, 1000) == 0);
    assert(button_filter_update(&f, 0, 2000) == 0);
    assert(button_filter_update(&f, 1, 3000) == 0);
    assert(button_filter_update(&f, 1, 7999) == 0);
    assert(button_filter_update(&f, 1, 8000) == 1);
    assert(button_filter_update(&f, 0x91, 9000) == 1);
    assert(button_filter_update(&f, 0x91, 14000) == 0x91);
    assert(button_filter_update(&f, 0x90, 15000) == 0x91);
    assert(button_filter_update(&f, 0x90, 20000) == 0x90);
    f = (button_filter_t){0};
    assert(button_filter_update(&f, 0x80, UINT32_MAX - 1000) == 0);
    assert(button_filter_update(&f, 0x80, 4000) == 0x80);
}

static void check_resampler(uint32_t clock)
{
    const size_t n = 100000;
    int16_t *fm = malloc(n * sizeof(*fm));
    int16_t *psg = malloc(n * sizeof(*psg));
    int16_t *whole = malloc(n * sizeof(*whole));
    int16_t *parts = malloc(n * sizeof(*parts));
    assert(fm && psg && whole && parts);
    for (size_t i = 0; i < n; ++i) {
        fm[i] = (int16_t)(i * 319U);
        psg[i] = (int16_t)(~(i * 179U));
    }
    audio_resample_t a = {0}, b = {0};
    const size_t total = audio_resample(&a, fm, psg, n, clock, whole, n);
    assert(total == (uint64_t)n * 48000U * 1009U / clock);
    size_t read = 0, written = 0;
    while (read < n) {
        size_t chunk = 37 + (read % 1000);
        if (chunk > n - read) {
            chunk = n - read;
        }
        const size_t count = audio_resample(&b, fm + read, psg + read, chunk, clock,
                                            parts + written, n - written);
        assert(count != SIZE_MAX);
        written += count;
        read += chunk;
    }
    assert(written == total);
    assert(memcmp(whole, parts, total * sizeof(*whole)) == 0);
    assert(a.phase == b.phase);
    audio_resample_t invalid = {0};
    int16_t guard[3] = {111, 222, 333};
    assert(audio_resample(&invalid, fm, psg, 10, clock, guard + 1, 0) == SIZE_MAX);
    assert(guard[0] == 111 && guard[1] == 222 && guard[2] == 333);
    assert(audio_resample(&invalid, fm, psg, 10, 0, guard + 1, 1) == SIZE_MAX);
    free(fm);
    free(psg);
    free(whole);
    free(parts);
}

int main(void)
{
    check_buttons();
    check_resampler(53693175);
    check_resampler(53203424);
    assert(game_hash32("hello", 5) == UINT32_C(0x4f9f2cab));
    puts("UTIL_CHECK PASS debounce/bounce/chords/timer-wrap NTSC/PAL resampling phase/capacity hash");
    return 0;
}
