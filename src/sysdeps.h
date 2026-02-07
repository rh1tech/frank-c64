/*
 *  sysdeps.h - Try to include the right system headers and get other
 *              system-specific stuff right
 *
 *  Frodo Copyright (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SYSDEPS_H
#define SYSDEPS_H

#include "sysconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <cstdint>
#include <string>
#else
#include <stdint.h>
#include <stdbool.h>
#endif

//=============================================================================
// Platform-Specific Headers
//=============================================================================

#ifdef FRODO_RP2350

// RP2350 Platform
#include "pico/stdlib.h"
#include "pico/time.h"

// Time functions for RP2350
#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t GetTicks_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static inline uint64_t GetTicks_us(void) {
    return to_us_since_boot(get_absolute_time());
}

static inline void Delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

#ifdef __cplusplus
}
#endif

// Disable SDL-dependent features
#define NO_SDL 1

#else  // Desktop platform

// SDL Platform
#include <SDL.h>

// Time functions using SDL
static inline uint32_t GetTicks_ms(void) {
    return SDL_GetTicks();
}

static inline uint64_t GetTicks_us(void) {
    return SDL_GetTicks() * 1000ULL;
}

static inline void Delay_ms(uint32_t ms) {
    SDL_Delay(ms);
}

#endif  // FRODO_RP2350

//=============================================================================
// Memory Allocation
//=============================================================================

#ifdef FRODO_RP2350

// Use PSRAM allocator for large allocations on RP2350
#ifdef __cplusplus
extern "C" {
#endif

#ifdef PSRAM_MAX_FREQ_MHZ
void *psram_malloc(size_t size);
void *psram_realloc(void *ptr, size_t size);
void psram_free(void *ptr);
#endif

#ifdef __cplusplus
}
#endif

// Macros for C64 memory allocation
#ifdef PSRAM_MAX_FREQ_MHZ
#define C64_MALLOC(size)        psram_malloc(size)
#define C64_FREE(ptr)           psram_free(ptr)
#define C64_REALLOC(ptr, size)  psram_realloc(ptr, size)
#endif

// File I/O wrappers for FatFS
#ifdef __cplusplus
extern "C" {
#endif

// FatFS-based file operations
typedef struct FATFS_FILE_HANDLE FATFS_FILE;
FATFS_FILE *fatfs_fopen(const char *path, const char *mode);
int fatfs_fclose(FATFS_FILE *fp);
size_t fatfs_fread(void *ptr, size_t size, size_t nmemb, FATFS_FILE *fp);
size_t fatfs_fwrite(const void *ptr, size_t size, size_t nmemb, FATFS_FILE *fp);
int fatfs_fseek(FATFS_FILE *fp, long offset, int whence);
long fatfs_ftell(FATFS_FILE *fp);
int fatfs_feof(FATFS_FILE *fp);
int fatfs_getc(FATFS_FILE *fp);
int fatfs_putc(int c, FATFS_FILE *fp);
void fatfs_rewind(FATFS_FILE *fp);

#ifdef __cplusplus
}
#endif

// Redirect stdio file operations to FatFS
#define FILE            FATFS_FILE
#define fopen           fatfs_fopen
#define fclose          fatfs_fclose
#define fread           fatfs_fread
#define fwrite          fatfs_fwrite
#define fseek           fatfs_fseek
#define ftell           fatfs_ftell
#define feof            fatfs_feof
#define getc            fatfs_getc
#define putc            fatfs_putc
#define rewind          fatfs_rewind

#else  // Desktop

// Standard allocation on desktop
#define C64_MALLOC(size)        malloc(size)
#define C64_FREE(ptr)           free(ptr)
#define C64_REALLOC(ptr, size)  realloc(ptr, size)

#endif  // FRODO_RP2350

#endif // ndef SYSDEPS_H
