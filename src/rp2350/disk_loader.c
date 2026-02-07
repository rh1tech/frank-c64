/*
 *  disk_loader.c - SD card disk image loader for RP2350
 *
 *  MurmC64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Scans SD card for D64/G64/T64 disk images and provides
 *  a simple interface for mounting them.
 */

#include "board_config.h"
#include "debug_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fatfs/ff.h"

//=============================================================================
// Configuration
//=============================================================================

#define MAX_DISK_IMAGES     100
#define MAX_FILENAME_LEN    64
#define DEFAULT_SCAN_PATH   "/c64"

//=============================================================================
// Disk Image Entry
//=============================================================================

typedef struct {
    char name[MAX_FILENAME_LEN];
    uint32_t size;
    uint8_t type;  // 0=D64, 1=G64, 2=T64, 3=TAP, 4=PRG, 5=CRT, 6=D81, 7=DIR
} disk_entry_t;

//=============================================================================
// State
//=============================================================================

static struct {
    disk_entry_t entries[MAX_DISK_IMAGES];
    int count;
    bool initialized;
} disk_loader;

char current_scan_path[128] = DEFAULT_SCAN_PATH;

//=============================================================================
// File Type Detection
//=============================================================================

static int detect_file_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return -1;

    if (strcasecmp(ext, ".d64") == 0) return 0;
    if (strcasecmp(ext, ".g64") == 0) return 1;
    if (strcasecmp(ext, ".t64") == 0) return 2;
    if (strcasecmp(ext, ".tap") == 0) return 3;
    if (strcasecmp(ext, ".prg") == 0) return 4;
    if (strcasecmp(ext, ".crt") == 0) return 5;
    if (strcasecmp(ext, ".d81") == 0) return 6;

    return -1;
}

//=============================================================================
// Public API
//=============================================================================

void disk_loader_init(void)
{
    memset(&disk_loader, 0, sizeof(disk_loader));
    disk_loader.initialized = true;
    strncpy(current_scan_path, DEFAULT_SCAN_PATH, sizeof(current_scan_path)-1);
    MII_DEBUG_PRINTF("Disk loader initialized\n");
}

static int disk_entry_cmp(const void *a, const void *b)
{
    const disk_entry_t *ea = (const disk_entry_t *)a;
    const disk_entry_t *eb = (const disk_entry_t *)b;
    // Directories first
    if (ea->type == 7 && eb->type != 7) return -1;
    if (ea->type != 7 && eb->type == 7) return 1;
    // Same type: sort by name (case-insensitive)
    return strcasecmp(ea->name, eb->name);
}

int disk_loader_scan_dir(const char *path)
{
    if (!disk_loader.initialized) {
        disk_loader_init();
    }

    if (path) {
        strncpy(current_scan_path, path, sizeof(current_scan_path)-1);
        current_scan_path[sizeof(current_scan_path)-1] = 0;
    }    
    disk_loader.count = 0;

    DIR dir;
    FILINFO fno;

    // Try to open disk directory
    FRESULT fr = f_opendir(&dir, current_scan_path);
    if (fr != FR_OK) {
        return -fr;
        MII_DEBUG_PRINTF("Failed to open directory for scanning\n");
    }

    MII_DEBUG_PRINTF("Scanning for disk images...\n");

    while (disk_loader.count < MAX_DISK_IMAGES) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;  // End of directory
        }

        disk_entry_t *entry = &disk_loader.entries[disk_loader.count];

        if (fno.fattrib & AM_DIR) {
            strncpy(entry->name, strlen(fno.fname) >= MAX_FILENAME_LEN-1 ? fno.altname : fno.fname, MAX_FILENAME_LEN-1);
            entry->size = 0;
            entry->type = 7;
            disk_loader.count++;
            continue;
        }

        // Check file type
        int type = detect_file_type(fno.fname);
        if (type < 0) {
            continue;
        }


        // Add to list
        strncpy(entry->name, strlen(fno.fname) >= MAX_FILENAME_LEN-1 ? fno.altname : fno.fname, MAX_FILENAME_LEN - 1);
        entry->size = fno.fsize;
        entry->type = type;

        MII_DEBUG_PRINTF("  Found: %s (%lu bytes)\n", entry->name, (unsigned long)entry->size);

        disk_loader.count++;
    }

    f_closedir(&dir);

    // Sort entries: directories first, then by name (case-insensitive)
    if (disk_loader.count > 1) {
        qsort(disk_loader.entries,
              disk_loader.count,
              sizeof(disk_entry_t),
              disk_entry_cmp);
    }

    MII_DEBUG_PRINTF("Found %d disk images\n", disk_loader.count);
    return disk_loader.count;
}

void disk_loader_scan(void)
{
    disk_loader_scan_dir(current_scan_path);
}

int disk_loader_get_count(void)
{
    return disk_loader.count;
}

const char *disk_loader_get_filename(int index)
{
    if (index < 0 || index >= disk_loader.count) {
        return NULL;
    }
    return disk_loader.entries[index].name;
}

const disk_entry_t *disk_loader_get_entry(int index)
{
    if (index < 0 || index >= disk_loader.count)
        return NULL;
    return &disk_loader.entries[index];
}

const char *disk_loader_get_cwd(void)
{
    return current_scan_path;
}

int disk_loader_delete(int index)
{
    const disk_entry_t *e = disk_loader_get_entry(index);
    if (!e)
        return -1;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", current_scan_path, e->name);
    return f_unlink(path);
}

// Returns full path to disk image (static buffer - not thread safe)
const char *disk_loader_get_path(int index)
{
    static char path_buffer[128];

    if (index < 0 || index >= disk_loader.count) {
        return NULL;
    }

    snprintf(path_buffer, sizeof(path_buffer), "%s/%s",
             current_scan_path, disk_loader.entries[index].name);

    return path_buffer;
}

uint32_t disk_loader_get_size(int index)
{
    if (index < 0 || index >= disk_loader.count) {
        return 0;
    }
    return disk_loader.entries[index].size;
}

int disk_loader_get_type(int index)
{
    if (index < 0 || index >= disk_loader.count) {
        return -1;
    }
    return disk_loader.entries[index].type;
}
