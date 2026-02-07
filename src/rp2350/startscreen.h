/*
 * startscreen.h - Welcome/start screen for MurmC64
 *
 * Displays system information on startup before emulation begins.
 *
 * Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 */

#ifndef STARTSCREEN_H
#define STARTSCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *title;
    const char *subtitle;
    const char *version;
    const char* board_variant;
    uint32_t cpu_mhz;
#ifdef PSRAM_MAX_FREQ_MHZ
    uint32_t psram_mhz;
#endif
} startscreen_info_t;

/**
 * Display the start screen with system information
 * Blocks for a few seconds before returning
 *
 * @param info System information to display
 * @return 0 on success
 */
int startscreen_show(startscreen_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // STARTSCREEN_H
