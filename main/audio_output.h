#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "audio_resample.h"
#include "gwenesis_port.h"

esp_err_t audio_output_init(void);
esp_err_t audio_output_frame(audio_resample_t *resampler, const gw_frame_t *frame);
esp_err_t audio_output_tone(void);
esp_err_t audio_output_finish(void);
void audio_output_stats(void);
