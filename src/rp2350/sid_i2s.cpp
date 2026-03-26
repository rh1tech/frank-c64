/*
 *  sid_i2s.cpp - SID audio output via I2S for RP2350
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Uses the murmgenesis audio driver (double-buffered DMA ping-pong).
 */

#include "../board_config.h"

extern "C" {
#include "debug_log.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "audio.h"
}

#include <cstring>
#include <cstdio>

//=============================================================================
// Configuration
//=============================================================================

// Ring buffer for SID samples - large enough for smooth playback
#define SID_RING_BUFFER_SIZE 4096

// Target samples per frame (PAL: 50Hz, NTSC: 60Hz)
// At 44100 Hz: PAL = 882 samples/frame, NTSC = 735 samples/frame
#define TARGET_SAMPLES_PAL   882
#define TARGET_SAMPLES_NTSC  735

//=============================================================================
// I2S Audio State
//=============================================================================

static struct {
    bool initialized;

    // Ring buffer for samples from SID emulation
    int16_t ring_buffer[SID_RING_BUFFER_SIZE * 2];  // Stereo
    volatile uint32_t write_index;
    volatile uint32_t read_index;

    // Last sample for crossfade (to prevent clicks)
    int16_t last_left;
    int16_t last_right;

} audio_state;

// Mixed buffer for output to I2S
static int16_t __attribute__((aligned(4))) mixed_buffer[AUDIO_BUFFER_SAMPLES * 2];

//=============================================================================
// I2S Audio Functions
//=============================================================================

extern "C" {

void sid_i2s_init(void)
{
    if (audio_state.initialized) {
        return;
    }

    memset(&audio_state, 0, sizeof(audio_state));

    // Initialize the murmgenesis audio driver
    if (!audio_init()) {
        MII_DEBUG_PRINTF("sid_i2s_init: audio_init failed\n");
        return;
    }

    audio_state.initialized = true;
    MII_DEBUG_PRINTF("SID I2S audio initialized (murmgenesis driver)\n");
}

void sid_i2s_update(void)
{
    if (!audio_state.initialized) {
        return;
    }

    // Memory barrier to ensure we see latest write_index
    __dmb();

    // Calculate how many samples are available in ring buffer
    uint32_t read_idx = audio_state.read_index;
    uint32_t write_idx = audio_state.write_index;
    int32_t available = (int32_t)(write_idx - read_idx);
    if (available < 0) available = 0;

    // Target samples per frame (use PAL timing for C64)
    int target_samples = TARGET_SAMPLES_PAL;

    // Fill output buffer from ring buffer
    for (int i = 0; i < target_samples; i++) {
        int16_t left = 0;
        int16_t right = 0;

        if (available > 0) {
            // Read from ring buffer
            uint32_t buf_idx = (read_idx & (SID_RING_BUFFER_SIZE - 1)) * 2;
            left = audio_state.ring_buffer[buf_idx];
            right = audio_state.ring_buffer[buf_idx + 1];
            read_idx++;
            available--;

            // Crossfade from last sample at start of buffer
            if (i < 16) {
                int fade_in = (i * 256) / 16;
                int fade_out = 256 - fade_in;
                left = (int16_t)((left * fade_in + audio_state.last_left * fade_out) >> 8);
                right = (int16_t)((right * fade_in + audio_state.last_right * fade_out) >> 8);
            }
        } else {
            // Buffer underrun: fade to silence
            left = (audio_state.last_left * 240) >> 8;
            right = (audio_state.last_right * 240) >> 8;
        }

        // Update last sample
        audio_state.last_left = left;
        audio_state.last_right = right;

        // Write to output buffer (stereo interleaved)
        mixed_buffer[i * 2] = left;
        mixed_buffer[i * 2 + 1] = right;
    }

    // Update read index
    __dmb();
    audio_state.read_index = read_idx;

#if defined(FEATURE_AUDIO_I2S)
    // Submit to I2S DMA
    i2s_config_t *config = audio_get_i2s_config();
    i2s_dma_write_count(config, mixed_buffer, target_samples);
#endif
#if defined(FEATURE_AUDIO_PWM)
    pwm_dma_write_count(mixed_buffer, target_samples);
#endif
}

// Add samples to the SID ring buffer (called from SID emulation)
void sid_add_sample(int16_t left, int16_t right)
{
    if (!audio_state.initialized) {
        return;
    }

    uint32_t write_idx = audio_state.write_index;
    uint32_t read_idx = audio_state.read_index;

    // Calculate available space (with wrap-around)
    uint32_t available = SID_RING_BUFFER_SIZE - ((write_idx - read_idx) & (SID_RING_BUFFER_SIZE - 1)) - 1;

    if (available > 0) {
        uint32_t buf_idx = (write_idx & (SID_RING_BUFFER_SIZE - 1)) * 2;
        audio_state.ring_buffer[buf_idx] = left;
        audio_state.ring_buffer[buf_idx + 1] = right;

        // Memory barrier before updating write index
        __dmb();
        audio_state.write_index = write_idx + 1;
    }
    // else: buffer full, drop sample
}

// Get current sample buffer fill level
int sid_get_buffer_fill(void)
{
    int32_t fill = (int32_t)(audio_state.write_index - audio_state.read_index);
    if (fill < 0) fill = 0;
    return (int)fill;
}

}  // extern "C"
