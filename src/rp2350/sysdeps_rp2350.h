/*
 *  sysdeps_rp2350.h - System-dependent definitions for RP2350
 *
 *  MurmC64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This file replaces SDL dependencies with RP2350-specific implementations.
 */

#ifndef SYSDEPS_RP2350_H
#define SYSDEPS_RP2350_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Platform Identification
//=============================================================================

#define FRODO_RP2350 1

//=============================================================================
// Time Functions (replacing SDL_GetTicks, etc.)
//=============================================================================

static inline uint32_t rp2350_get_ticks_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static inline uint64_t rp2350_get_ticks_us(void) {
    return to_us_since_boot(get_absolute_time());
}

static inline void rp2350_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

static inline void rp2350_delay_us(uint32_t us) {
    sleep_us(us);
}

//=============================================================================
// Atomic Operations
//=============================================================================

// RP2350 has hardware spinlocks, but for simple flags we can use volatile
#define ATOMIC_LOAD(ptr)        (*(volatile typeof(*(ptr)) *)(ptr))
#define ATOMIC_STORE(ptr, val)  (*(volatile typeof(*(ptr)) *)(ptr) = (val))

//=============================================================================
// Debug Output
//=============================================================================

#if ENABLE_DEBUG_LOGS
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // SYSDEPS_RP2350_H
