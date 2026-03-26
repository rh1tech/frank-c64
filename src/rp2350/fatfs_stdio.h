/*
 *  fatfs_stdio.h - FatFS wrapper for stdio file operations
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Provides a thin wrapper around FatFS to make stdio-based code work
 *  on RP2350. Maps fopen/fread/fwrite/fseek/ftell/fclose to FatFS equivalents.
 */

#ifndef FATFS_STDIO_H
#define FATFS_STDIO_H

#include "ff.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// File handle wrapper - the actual struct that FATFS_FILE points to
typedef struct FATFS_FILE_HANDLE {
    FIL fil;
    int is_open;
} FATFS_FILE;

// Open a file
FATFS_FILE *fatfs_fopen(const char *path, const char *mode);

// Close a file
int fatfs_fclose(FATFS_FILE *fp);

// Read from file
size_t fatfs_fread(void *ptr, size_t size, size_t nmemb, FATFS_FILE *fp);

// Write to file
size_t fatfs_fwrite(const void *ptr, size_t size, size_t nmemb, FATFS_FILE *fp);

// Seek in file
int fatfs_fseek(FATFS_FILE *fp, long offset, int whence);

// Get current position
long fatfs_ftell(FATFS_FILE *fp);

// Check for EOF
int fatfs_feof(FATFS_FILE *fp);

// Get a single character
int fatfs_getc(FATFS_FILE *fp);

// Put a single character
int fatfs_putc(int c, FATFS_FILE *fp);

// Rewind to beginning
void fatfs_rewind(FATFS_FILE *fp);

#ifdef __cplusplus
}
#endif

#endif // FATFS_STDIO_H
