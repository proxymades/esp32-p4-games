#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GW_WIDTH 320
#define GW_HEIGHT 240
#define GW_AUDIO_CAPACITY 2048

typedef void *(*gw_alloc_fn)(size_t bytes, bool external);
typedef void (*gw_free_fn)(void *memory);
typedef struct {
    unsigned width;
    unsigned height;
    unsigned master_clock_hz;
    unsigned frame_cycles;
    unsigned audio_samples;
    const int16_t *fm;
    const int16_t *psg;
    uint32_t program_counter;
    uint32_t logic_cycles;
    uint32_t synth_cycles;
    uint32_t video_cycles;
    uint32_t rgb_cycles;
    bool pal;
} gw_frame_t;

/* One instance, owned by the emulation task. ROM is mutable and word-swapped
   in place by upstream load_cartridge(). The caller owns the ROM allocation. */
bool gw_rom_valid(const uint8_t *rom, size_t length);
bool gw_init(uint8_t *rom, size_t length, gw_alloc_fn allocate, gw_free_fn release);
/* NULL RGB skips pixels while retaining sprite evaluation and VDP state. */
bool gw_frame(uint8_t buttons, uint16_t *rgb565, gw_frame_t *result);
void gw_shutdown(void);
