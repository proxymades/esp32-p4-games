#include "gwenesis_port.h"
#include "gwenesis_profile.h"
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_vdp.h"
#include "ym2612.h"
#include "z80inst.h"
#include "gwenesis_sn76489.h"
#include "gwenesis_savestate.h"

unsigned char *M68K_RAM;
unsigned char *ZRAM;
int16_t *gwenesis_sn76489_buffer;
int16_t *gwenesis_ym2612_buffer;
int sn76489_clock;
int sn76489_index;
int ym2612_clock;
int ym2612_index;
int32_t *lfo_pm_table;
signed int *tl_tab;
int scan_line;
int system_clock;
int frame_counter;
int gwenesis_audio_overflow;
uint32_t gw_synth_cycles;

extern unsigned char *gwenesis_vdp_regs;
extern unsigned short gwenesis_vdp_status;
extern unsigned short *CRAM565;
extern unsigned short *CRAM;
extern unsigned short *VSRAM;
extern unsigned short *fifo;
extern unsigned char *SAT_CACHE;
extern unsigned char *VRAM;
extern uint8_t *OPNREGS;
extern uint8_t io_reg[16];
extern int screen_width;
extern int screen_height;
extern int hint_pending;

static uint8_t *s_indexed;
static bool s_started;
static bool s_pal;
static gw_free_fn s_release;
static void **s_allocations[24];
static size_t s_allocation_count;

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

bool gw_rom_valid(const uint8_t *rom, size_t length)
{
    if (rom == NULL || length < 512 || length > MAX_ROM_SIZE || (length & 1U) != 0) {
        return false;
    }
    /* A zero initial SP is valid for this Comix Zone revision; its boot code
       initializes the stack. Validate the reset vector rather than assuming SP. */
    const uint32_t pc = be32(rom + 4);
    return memcmp(rom + 0x100, "SEGA", 4) == 0 && pc >= 0x200 &&
           pc < length && (pc & 1U) == 0;
}

static bool allocate_one(void **target, size_t bytes, bool external, gw_alloc_fn alloc)
{
    if (s_allocation_count >= sizeof(s_allocations) / sizeof(s_allocations[0])) {
        return false;
    }
    *target = alloc(bytes, external);
    if (*target == NULL) {
        return false;
    }
    memset(*target, 0, bytes);
    s_allocations[s_allocation_count++] = target;
    return true;
}

#define ALLOC(pointer, bytes, external) do { \
    if (!allocate_one((void **)&(pointer), (bytes), (external), allocate)) { \
        gw_shutdown(); \
        return false; \
    } \
} while (0)

bool gw_init(uint8_t *rom, size_t length, gw_alloc_fn allocate, gw_free_fn release)
{
    if (s_started || s_allocation_count != 0 || allocate == NULL || release == NULL ||
        !gw_rom_valid(rom, length)) {
        return false;
    }
    s_release = release;
    /* Allocate the largest contiguous block before smaller hot-state buffers. */
    ALLOC(s_indexed, GW_WIDTH * GW_HEIGHT, false);
    ALLOC(M68K_RAM, MAX_RAM_SIZE, false);
    ALLOC(VRAM, VRAM_MAX_SIZE, false);
    ALLOC(m68k, sizeof(*m68k), false);
    ALLOC(ZRAM, MAX_Z80_RAM_SIZE, false);
    ALLOC(CRAM, CRAM_MAX_SIZE * sizeof(uint16_t), false);
    ALLOC(CRAM565, CRAM_MAX_SIZE * 4 * sizeof(uint16_t), false);
    ALLOC(VSRAM, VSRAM_MAX_SIZE * sizeof(uint16_t), false);
    ALLOC(SAT_CACHE, SAT_CACHE_MAX_SIZE, false);
    ALLOC(gwenesis_vdp_regs, REG_SIZE, false);
    ALLOC(fifo, FIFO_SIZE * sizeof(uint16_t), false);
    ALLOC(render_buffer, SCREEN_WIDTH + PIX_OVERFLOW * 2, false);
    ALLOC(sprite_buffer, SCREEN_WIDTH + PIX_OVERFLOW * 2, false);
    ALLOC(ym2612, sizeof(*ym2612), false);
    ALLOC(OPNREGS, 512, false);
    ALLOC(sin_tab, SIN_LEN * sizeof(*sin_tab), false);
    ALLOC(tl_tab, 13 * 2 * 256 * sizeof(*tl_tab), false);
    ALLOC(lfo_pm_table, 128 * 8 * 32 * sizeof(*lfo_pm_table), true);
    ALLOC(gwenesis_sn76489_buffer, GW_AUDIO_CAPACITY * sizeof(int16_t), false);
    ALLOC(gwenesis_ym2612_buffer, GW_AUDIO_CAPACITY * sizeof(int16_t), false);

    load_cartridge(rom, length);
    power_on();
    reset_emulation();
    s_pal = (io_reg[0] & 0x40U) != 0;
    const int master = s_pal ? MCLOCK_PAL : MCLOCK_NTSC;
    gwenesis_SN76489_Init(master / 15, master / AUDIO_FREQ_DIVISOR, AUDIO_FREQ_DIVISOR);
    gwenesis_vdp_set_buffer(s_indexed);
    sn76489_clock = 0;
    ym2612_clock = 0;
    sn76489_index = 0;
    ym2612_index = 0;
    gwenesis_audio_overflow = 0;
    frame_counter = 0;
    system_clock = 0;
    s_started = true;
    return true;
}

bool gw_frame(uint8_t buttons, uint16_t *rgb565, gw_frame_t *result)
{
    if (!s_started || result == NULL) {
        return false;
    }
    static const uint8_t pad_map[8] = {
        PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_C, PAD_S
    };
    for (unsigned key = 0; key < 8; ++key) {
        if (buttons & (1U << key)) {
            gwenesis_io_pad_press_button(0, pad_map[key]);
        } else {
            gwenesis_io_pad_release_button(0, pad_map[key]);
        }
    }
    screen_width = REG12_MODE_H40 ? 320 : 256;
    screen_height = REG1_240_LINE ? 240 : 224;
    const int height = screen_height;
    const int width = screen_width;
    const int y_offset = (GW_HEIGHT - height) / 2;
    const int x_offset = (GW_WIDTH - width) / 2;
    if (rgb565 != NULL) {
        /* The active image is overwritten below. Clear only static borders,
           avoiding a full-frame cache fill before the CPU interpreters run. */
        memset(rgb565, 0, (size_t)y_offset * GW_WIDTH * sizeof(*rgb565));
        memset(rgb565 + (y_offset + height) * GW_WIDTH, 0,
               (size_t)y_offset * GW_WIDTH * sizeof(*rgb565));
    }
    const int total_lines = s_pal ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
    int hint_counter = gwenesis_vdp_regs[10];
    gwenesis_vdp_render_config();
    system_clock = 0;
    sn76489_index = 0;
    ym2612_index = 0;
    uint32_t logic_cycles = 0;
    uint32_t video_cycles = 0;
    uint32_t rgb_cycles = 0;
    gw_synth_cycles = 0;
    for (scan_line = 0; scan_line < total_lines; ++scan_line) {
        system_clock += VDP_CYCLES_PER_LINE;
        uint32_t logic_start = gw_cycle_now();
        uint32_t synth_before = gw_synth_cycles;
        m68k_run((unsigned)system_clock);
        z80_run(system_clock);
        logic_cycles += gw_cycle_now() - logic_start - (gw_synth_cycles - synth_before);
        /* All FM/PSG accesses and synthesis remain in this task. */
        gwenesis_SN76489_run(system_clock);
        ym2612_run(system_clock);
        uint32_t video_start = gw_cycle_now();
        if (scan_line < height) {
            if (rgb565 != NULL) {
                gwenesis_vdp_render_line(scan_line);
            } else {
                gwenesis_vdp_evaluate_line(scan_line);
            }
            if (rgb565 != NULL) {
                const uint32_t rgb_start = gw_cycle_now();
                uint16_t *row = rgb565 + (scan_line + y_offset) * GW_WIDTH;
                memset(row, 0, (size_t)x_offset * sizeof(*row));
                memset(row + x_offset + width, 0, (size_t)x_offset * sizeof(*row));
                uint16_t *dest = row + x_offset;
                const uint8_t *src = s_indexed + scan_line * GW_WIDTH;
                for (int x = 0; x < width; ++x) {
                    dest[x] = CRAM565[src[x]];
                }
                rgb_cycles += gw_cycle_now() - rgb_start;
            }
        }
        video_cycles += gw_cycle_now() - video_start;
        if (scan_line == 0 || scan_line > height) {
            hint_counter = REG10_LINE_COUNTER;
        }
        if (--hint_counter < 0) {
            if (REG0_LINE_INTERRUPT && scan_line <= height) {
                hint_pending = 1;
                if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0) {
                    m68k_update_irq(4);
                }
            }
            hint_counter = REG10_LINE_COUNTER;
        }
        if (scan_line + 1 == height) {
            if (REG1_VBLANK_INTERRUPT) {
                gwenesis_vdp_status |= STATUS_VIRQPENDING;
                m68k_set_irq(6);
            }
            z80_irq_line(1);
        }
        if (scan_line + 1 == height + 1) {
            z80_irq_line(0);
        }
    }
    /* A final Z80 instruction can write audio just past the frame boundary.
       Bring both synths to the same sample time before mixing that lookahead. */
    int audio_end = system_clock;
    if (ym2612_clock > audio_end) {
        audio_end = ym2612_clock;
    }
    if (sn76489_clock > audio_end) {
        audio_end = sn76489_clock;
    }
    gwenesis_SN76489_run(audio_end);
    ym2612_run(audio_end);
    m68k->cycles -= system_clock;
    zclk -= system_clock;
    /* Carry sub-sample phase across frame boundaries instead of dropping it. */
    sn76489_clock -= system_clock;
    ym2612_clock -= system_clock;
    *result = (gw_frame_t) {
        .width = (unsigned)width, .height = (unsigned)height,
        .master_clock_hz = s_pal ? MCLOCK_PAL : MCLOCK_NTSC,
        .frame_cycles = (unsigned)system_clock,
        .audio_samples = (unsigned)(sn76489_index > ym2612_index ? sn76489_index : ym2612_index),
        .fm = gwenesis_ym2612_buffer, .psg = gwenesis_sn76489_buffer,
        .program_counter = m68k_get_reg(M68K_REG_PC),
        .logic_cycles = logic_cycles, .synth_cycles = gw_synth_cycles, .video_cycles = video_cycles, .rgb_cycles = rgb_cycles,
        .pal = s_pal,
    };
    ++frame_counter;
    return gwenesis_audio_overflow == 0 && sn76489_index == ym2612_index &&
           result->audio_samples < GW_AUDIO_CAPACITY;
}

void gw_shutdown(void)
{
    while (s_allocation_count != 0) {
        void **slot = s_allocations[--s_allocation_count];
        s_release(*slot);
        *slot = NULL;
    }
    s_started = false;
}

/* Input is latched by gw_frame; save-state services are deliberately absent. */
void gwenesis_io_get_buttons(void) {}
SaveState *saveGwenesisStateOpenForRead(const char *name) { (void)name; return NULL; }
SaveState *saveGwenesisStateOpenForWrite(const char *name) { (void)name; return NULL; }
int saveGwenesisStateGet(SaveState *state, const char *name) { (void)state; (void)name; return 0; }
void saveGwenesisStateSet(SaveState *state, const char *name, int value) { (void)state; (void)name; (void)value; }
void saveGwenesisStateGetBuffer(SaveState *state, const char *name, void *buffer, int length)
{ (void)state; (void)name; (void)buffer; (void)length; }
void saveGwenesisStateSetBuffer(SaveState *state, const char *name, const void *buffer, int length)
{ (void)state; (void)name; (void)buffer; (void)length; }
