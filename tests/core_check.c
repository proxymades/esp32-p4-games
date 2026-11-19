#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gwenesis_port.h"
#include "audio_resample.h"
#include "checksum.h"

static void *allocate(size_t bytes, bool external)
{
    (void)external;
    return calloc(1, bytes);
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: core_check ROM frames output-directory [render-every]\n");
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    assert(input != NULL);
    assert(fseek(input, 0, SEEK_END) == 0);
    const long size = ftell(input);
    rewind(input);
    assert(size > 0);
    uint8_t *rom = malloc((size_t)size);
    assert(rom != NULL);
    assert(fread(rom, 1, (size_t)size, input) == (size_t)size);
    fclose(input);
    assert(gw_rom_valid(rom, (size_t)size));
    assert(!gw_rom_valid(rom, 1));
    uint16_t *pixels = malloc(GW_WIDTH * GW_HEIGHT * sizeof(*pixels));
    assert(pixels != NULL);
    assert(gw_init(rom, (size_t)size, allocate, free));
    int16_t pcm[1024];
    audio_resample_t resampler = {0};
    char path[1024];
    snprintf(path, sizeof(path), "%s/audio.s16le", argv[3]);
    FILE *audio = fopen(path, "wb");
    assert(audio != NULL);
    uint64_t samples = 0;
    const unsigned frames = (unsigned)strtoul(argv[2], NULL, 10);
    const unsigned render_every = argc == 5 ? (unsigned)strtoul(argv[4], NULL, 10) : 1;
    assert(render_every > 0);
    for (unsigned i = 1; i <= frames; ++i) {
        const uint8_t keys = (i >= 1900 && i < 1905) ? 0x80 : 0;
        gw_frame_t result;
        const bool render = i % render_every == 0 || i % 120 == 0 ||
                            i == 900 || i == frames;
        if (!gw_frame(keys, render ? pixels : NULL, &result)) {
            fprintf(stderr, "core failed frame=%u samples=%u PC=%06" PRIx32 "\n",
                    i, result.audio_samples, result.program_counter);
            abort();
        }
        const size_t n = audio_resample(&resampler, result.fm, result.psg,
                                        result.audio_samples, result.master_clock_hz, pcm, 1024);
        assert(n != SIZE_MAX);
        assert(fwrite(pcm, sizeof(*pcm), n, audio) == n);
        samples += n;
        if (i % 120 == 0 || i == frames) {
            printf("FRAME %u hash=%08" PRIx32 " pc=%06" PRIx32 " video=%ux%u synth=%u pcm=%" PRIu64 "\n",
                   i, game_hash32(pixels, GW_WIDTH * GW_HEIGHT * sizeof(*pixels)),
                   result.program_counter, result.width, result.height, result.audio_samples, samples);
            printf("AUDIO_FRAME %u fm_hash=%08" PRIx32 " psg_hash=%08" PRIx32 "\n",
                   i, game_hash32(result.fm, result.audio_samples * sizeof(*result.fm)),
                   game_hash32(result.psg, result.audio_samples * sizeof(*result.psg)));
            fflush(stdout);
        }
        if (i == 120 || i == 360 || i == 600 || i == 900 || i == 2400 || i == 4200 || i == 5400 || i == frames) {
            snprintf(path, sizeof(path), "%s/frame-%04u.rgb565", argv[3], i);
            FILE *image = fopen(path, "wb");
            assert(image != NULL);
            assert(fwrite(pixels, sizeof(*pixels), GW_WIDTH * GW_HEIGHT, image) == GW_WIDTH * GW_HEIGHT);
            fclose(image);
        }
    }
    fclose(audio);
    gw_shutdown();
    free(pixels);
    free(rom);
    printf("CORE_CHECK PASS frames=%u pcm_samples=%" PRIu64 "\n", frames, samples);
    return 0;
}
