/*
 *  fatfs_stdio.c - FatFS wrapper for stdio file operations
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "fatfs_stdio.h"
#include <string.h>
#include <stdlib.h>
#include "debug_log.h"

// Pool of file handles (FatFS doesn't support malloc for FIL structures well)
#define MAX_OPEN_FILES 4
static FATFS_FILE file_pool[MAX_OPEN_FILES];
static int pool_initialized = 0;

static void init_pool(void) {
    if (!pool_initialized) {
        memset(file_pool, 0, sizeof(file_pool));
        pool_initialized = 1;
    }
}

static FATFS_FILE *alloc_file(void) {
    init_pool();
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_pool[i].is_open) {
            return &file_pool[i];
        }
    }
    return NULL;  // No free slots
}

FATFS_FILE *fatfs_fopen(const char *path, const char *mode) {
    if (!path || !mode) {
        MII_DEBUG_PRINTF("fatfs_fopen: null path or mode\n");
        return NULL;
    }

    MII_DEBUG_PRINTF("fatfs_fopen: opening '%s' mode='%s'\n", path, mode);

    FATFS_FILE *fp = alloc_file();
    if (!fp) {
        MII_DEBUG_PRINTF("fatfs_fopen: no free file slots\n");
        return NULL;
    }

    // Parse mode string
    BYTE fatfs_mode = 0;
    if (strchr(mode, 'r')) {
        fatfs_mode |= FA_READ;
        if (strchr(mode, '+')) {
            fatfs_mode |= FA_WRITE;
        }
    } else if (strchr(mode, 'w')) {
        fatfs_mode |= FA_WRITE | FA_CREATE_ALWAYS;
        if (strchr(mode, '+')) {
            fatfs_mode |= FA_READ;
        }
    } else if (strchr(mode, 'a')) {
        fatfs_mode |= FA_WRITE | FA_OPEN_APPEND;
        if (strchr(mode, '+')) {
            fatfs_mode |= FA_READ;
        }
    }

    // Open file
    FRESULT fr = f_open(&fp->fil, path, fatfs_mode);
    if (fr != FR_OK) {
        MII_DEBUG_PRINTF("fatfs_fopen: f_open failed with error %d\n", fr);
        return NULL;
    }

    MII_DEBUG_PRINTF("fatfs_fopen: success, size=%lu\n", (unsigned long)f_size(&fp->fil));
    fp->is_open = 1;
    return fp;
}

int fatfs_fclose(FATFS_FILE *fp) {
    if (!fp || !fp->is_open) return -1;

    FRESULT fr = f_close(&fp->fil);
    fp->is_open = 0;

    return (fr == FR_OK) ? 0 : -1;
}

size_t fatfs_fread(void *ptr, size_t size, size_t nmemb, FATFS_FILE *fp) {
    if (!fp || !fp->is_open || !ptr) return 0;

    UINT bytes_read;
    size_t total = size * nmemb;

    FRESULT fr = f_read(&fp->fil, ptr, total, &bytes_read);
    if (fr != FR_OK) return 0;

    return bytes_read / size;
}

size_t fatfs_fwrite(const void *ptr, size_t size, size_t nmemb, FATFS_FILE *fp) {
    if (!fp || !fp->is_open || !ptr) return 0;

    UINT bytes_written;
    size_t total = size * nmemb;

    FRESULT fr = f_write(&fp->fil, ptr, total, &bytes_written);
    if (fr != FR_OK) return 0;

    return bytes_written / size;
}

int fatfs_fseek(FATFS_FILE *fp, long offset, int whence) {
    if (!fp || !fp->is_open) return -1;

    FSIZE_t new_pos;

    switch (whence) {
        case 0:  // SEEK_SET
            new_pos = offset;
            break;
        case 1:  // SEEK_CUR
            new_pos = f_tell(&fp->fil) + offset;
            break;
        case 2:  // SEEK_END
            new_pos = f_size(&fp->fil) + offset;
            break;
        default:
            return -1;
    }

    FRESULT fr = f_lseek(&fp->fil, new_pos);
    return (fr == FR_OK) ? 0 : -1;
}

long fatfs_ftell(FATFS_FILE *fp) {
    if (!fp || !fp->is_open) return -1;
    return (long)f_tell(&fp->fil);
}

int fatfs_feof(FATFS_FILE *fp) {
    if (!fp || !fp->is_open) return 1;
    return f_eof(&fp->fil);
}

int fatfs_getc(FATFS_FILE *fp) {
    if (!fp || !fp->is_open) return -1;

    uint8_t c;
    UINT bytes_read;
    FRESULT fr = f_read(&fp->fil, &c, 1, &bytes_read);
    if (fr != FR_OK || bytes_read != 1) return -1;

    return c;
}

int fatfs_putc(int c, FATFS_FILE *fp) {
    if (!fp || !fp->is_open) return -1;

    uint8_t byte = (uint8_t)c;
    UINT bytes_written;
    FRESULT fr = f_write(&fp->fil, &byte, 1, &bytes_written);
    if (fr != FR_OK || bytes_written != 1) return -1;

    return c;
}

void fatfs_rewind(FATFS_FILE *fp) {
    if (!fp || !fp->is_open) return;
    f_lseek(&fp->fil, 0);
}
