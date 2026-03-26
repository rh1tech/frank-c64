/*
 *  d64_drive.c - 1541 emulation in disk image files (.d64/.x64)
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Based on Frodo4 by Christian Bauer.
 *
 * Incompatibilities:
 * - No support for relative files
 * - Unimplemented commands: P
 * - Impossible to implement: B-E, M-E
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "d64_drive.h"
#include "iec.h"
#include "debug_log.h"

/*
 * Interleave values
 */
#define DIR_INTERLEAVE  3
#define DATA_INTERLEAVE 10

/*
 * Sectors per track for all tracks
 */
static const uint8_t num_sectors[41] = {
    0,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,  // 1-17
    19,19,19,19,19,19,19,                                  // 18-24
    18,18,18,18,18,18,                                      // 25-30
    17,17,17,17,17,                                          // 31-35
    17,17,17,17,17                                            // 36-40
};

/*
 * Accumulated number of sectors
 */
static const uint16_t accum_num_sectors[41] = {
    0,
    0,21,42,63,84,105,126,147,168,189,210,231,252,273,294,315,336,
    357,376,395,414,433,452,471,
    490,508,526,544,562,580,
    598,615,632,649,666,
    683,700,717,734,751
};

/*
 * Error messages
 */
static const char *error_messages[] = {
    "00,OK,%02d,%02d\r",
    "01,FILES SCRATCHED,%02d,%02d\r",
    "03,UNIMPLEMENTED,%02d,%02d\r",
    "20,READ ERROR,%02d,%02d\r",
    "21,READ ERROR,%02d,%02d\r",
    "22,READ ERROR,%02d,%02d\r",
    "23,READ ERROR,%02d,%02d\r",
    "24,READ ERROR,%02d,%02d\r",
    "25,WRITE ERROR,%02d,%02d\r",
    "26,WRITE PROTECT ON,%02d,%02d\r",
    "27,READ ERROR,%02d,%02d\r",
    "28,WRITE ERROR,%02d,%02d\r",
    "29,DISK ID MISMATCH,%02d,%02d\r",
    "30,SYNTAX ERROR,%02d,%02d\r",
    "31,SYNTAX ERROR,%02d,%02d\r",
    "32,SYNTAX ERROR,%02d,%02d\r",
    "33,SYNTAX ERROR,%02d,%02d\r",
    "34,SYNTAX ERROR,%02d,%02d\r",
    "60,WRITE FILE OPEN,%02d,%02d\r",
    "61,FILE NOT OPEN,%02d,%02d\r",
    "62,FILE NOT FOUND,%02d,%02d\r",
    "63,FILE EXISTS,%02d,%02d\r",
    "64,FILE TYPE MISMATCH,%02d,%02d\r",
    "65,NO BLOCK,%02d,%02d\r",
    "66,ILLEGAL TRACK OR SECTOR,%02d,%02d\r",
    "70,NO CHANNEL,%02d,%02d\r",
    "71,DIR ERROR,%02d,%02d\r",
    "72,DISK FULL,%02d,%02d\r",
    "73,FRANK_C64 VIRTUAL 1541,%02d,%02d\r",
    "74,DRIVE NOT READY,%02d,%02d\r"
};

/*
 * File type characters
 */
static const char type_char_1[] = "DSPUREER";
static const char type_char_2[] = "EERSELQG";
static const char type_char_3[] = "LQGRL???";

/*
 * Job error to error code conversion
 */
static const iec_error_t conv_job_error[16] = {
    ERR_OK,             // 0
    ERR_OK,             // 1
    ERR_READ20,         // 2
    ERR_READ21,         // 3
    ERR_READ22,         // 4
    ERR_READ23,         // 5
    ERR_READ24,         // 6
    ERR_WRITE25,        // 7
    ERR_WRITEPROTECT,   // 8
    ERR_READ27,         // 9
    ERR_WRITE28,        // 10
    ERR_DISKID,         // 11
    ERR_OK,             // 12
    ERR_OK,             // 13
    ERR_OK,             // 14
    ERR_NOTREADY        // 15
};

/*
 * Forward declarations
 */
static void close_all_channels(iec_drive_t *drive);
static int alloc_buffer(iec_drive_t *drive, int want);
static void free_buffer(iec_drive_t *drive, int buf);
static bool read_sector(iec_drive_t *drive, int track, int sector, uint8_t *buffer);
static bool write_sector(iec_drive_t *drive, int track, int sector, uint8_t *buffer);
static bool find_first_file(iec_drive_t *drive, const uint8_t *pattern, int pattern_len,
                           int *dir_track, int *dir_sector, int *entry);
static bool find_next_file(iec_drive_t *drive, const uint8_t *pattern, int pattern_len,
                          int *dir_track, int *dir_sector, int *entry);
static bool is_block_free(iec_drive_t *drive, int track, int sector);
static int num_free_blocks(iec_drive_t *drive, int track);
static int alloc_block(iec_drive_t *drive, int track, int sector);
static int free_block(iec_drive_t *drive, int track, int sector);
static bool alloc_next_block(iec_drive_t *drive, int *track, int *sector, int interleave);
static bool parse_image_file(iec_drive_t *drive);

/*
 * Get sectors per track
 */
int d64_sectors_per_track(int track)
{
    if (track < 1 || track > 40) return 0;
    return num_sectors[track];
}

/*
 * Convert track/sector to byte offset
 */
uint32_t d64_offset_from_ts(const image_file_desc_t *desc, int track, int sector)
{
    // D81: Simple linear layout - all tracks have 40 sectors
    if (desc->type == IMAGE_TYPE_D81) {
        if (track < 1 || track > D81_NUM_TRACKS ||
            sector < 0 || sector >= D81_SECTORS_PER_TRACK) {
            return 0xFFFFFFFF;  // Invalid
        }
        return (((track - 1) * D81_SECTORS_PER_TRACK + sector) << 8) + desc->header_size;
    }

    // D64/X64: Variable sectors per track
    if (track < 1 || track > desc->num_tracks ||
        sector < 0 || sector >= num_sectors[track]) {
        return 0xFFFFFFFF;  // Invalid
    }
    return ((accum_num_sectors[track] + sector) << 8) + desc->header_size;
}

/*
 * Convert D64 error info to error code
 */
iec_error_t d64_conv_error_info(uint8_t error)
{
    return conv_job_error[error & 0x0f];
}

/*
 * Set error message
 */
void d64_drive_set_error(iec_drive_t *drive, iec_error_t error, int track, int sector)
{
    snprintf(drive->error_buf, sizeof(drive->error_buf),
             error_messages[error], track, sector);
    drive->error_ptr = drive->error_buf;
    drive->error_len = strlen(drive->error_buf);
    drive->current_error = error;

    // Set drive LED condition
    if (error != ERR_OK && error != ERR_SCRATCHED) {
        if (error == ERR_STARTUP) {
            drive->led = DRVLED_OFF;
        } else {
            drive->led = DRVLED_ERROR_FLASH;
        }
    } else if (drive->led == DRVLED_ERROR_FLASH) {
        drive->led = DRVLED_OFF;
    }

    iec_update_leds();
}

/*
 * Create and initialize drive
 */
iec_drive_t *d64_drive_create(void)
{
    iec_drive_t *drive = (iec_drive_t *)malloc(sizeof(iec_drive_t));
    if (drive == NULL) {
        MII_DEBUG_PRINTF("D64: Failed to allocate drive structure\n");
        return NULL;
    }

    memset(drive, 0, sizeof(iec_drive_t));

    // Initialize defaults
    drive->led = DRVLED_OFF;
    drive->ready = false;
    drive->file_open = false;
    drive->write_protected = true;  // Default to write-protected
    drive->bam = drive->ram + 0x700;  // BAM at end of RAM
    drive->bam_dirty = false;

    // Initialize channels
    for (int i = 0; i < MAX_CHANNELS; i++) {
        drive->ch[i].mode = CHMOD_FREE;
        drive->ch[i].buf = NULL;
    }
    drive->ch[15].mode = CHMOD_COMMAND;

    // Initialize buffers as free
    for (int i = 0; i < NUM_BUFFERS; i++) {
        drive->buf_free[i] = true;
    }

    // Set startup error
    d64_drive_set_error(drive, ERR_STARTUP, 0, 0);

    return drive;
}

/*
 * Destroy drive
 */
void d64_drive_destroy(iec_drive_t *drive)
{
    if (drive == NULL) return;

    d64_drive_unmount(drive);

    if (drive->dir_buf != NULL) {
        free(drive->dir_buf);
    }

    free(drive);
}

/*
 * Reset drive
 */
void d64_drive_reset(iec_drive_t *drive)
{
    if (drive == NULL) return;

    close_all_channels(drive);
    drive->cmd_len = 0;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        drive->buf_free[i] = true;
    }

    // Write back BAM if dirty
    if (drive->file_open) {
        if (drive->desc.type == IMAGE_TYPE_D81) {
            if (drive->bam_dirty) {
                write_sector(drive, D81_DIR_TRACK, 1, drive->bam);
                drive->bam_dirty = false;
            }
            if (drive->bam2_dirty) {
                write_sector(drive, D81_DIR_TRACK, 2, drive->bam2);
                drive->bam2_dirty = false;
            }
        } else if (drive->bam_dirty) {
            write_sector(drive, DIR_TRACK, 0, drive->bam);
            drive->bam_dirty = false;
        }
    }

    memset(drive->ram, 0, sizeof(drive->ram));

    // Re-read BAM if mounted
    if (drive->file_open) {
        if (drive->desc.type == IMAGE_TYPE_D81) {
            read_sector(drive, D81_DIR_TRACK, 1, drive->bam);
            read_sector(drive, D81_DIR_TRACK, 2, drive->bam2);
        } else {
            read_sector(drive, DIR_TRACK, 0, drive->bam);
        }
    }

    d64_drive_set_error(drive, ERR_STARTUP, 0, 0);
}

/*
 * Mount disk image
 */
bool d64_drive_mount(iec_drive_t *drive, const char *path)
{
    FRESULT fr;

    if (drive == NULL || path == NULL) return false;

    MII_DEBUG_PRINTF("D64: Mounting %s\n", path);

    // Unmount current image
    d64_drive_unmount(drive);

    // Try to open the file for reading/writing first
    drive->write_protected = false;
    fr = f_open(&drive->file, path, FA_READ | FA_WRITE);
    if (fr != FR_OK) {
        // Try read-only
        drive->write_protected = true;
        fr = f_open(&drive->file, path, FA_READ);
        if (fr != FR_OK) {
            MII_DEBUG_PRINTF("D64: Failed to open %s: %d\n", path, fr);
            return false;
        }
    }

    drive->file_open = true;
    strncpy(drive->image_path, path, sizeof(drive->image_path) - 1);

    // Parse image file
    if (!parse_image_file(drive)) {
        f_close(&drive->file);
        drive->file_open = false;
        return false;
    }

    // Read BAM
    if (drive->desc.type == IMAGE_TYPE_D81) {
        // D81: BAM is at track 40, sectors 1 and 2
        if (!read_sector(drive, D81_DIR_TRACK, 1, drive->bam)) {
            f_close(&drive->file);
            drive->file_open = false;
            return false;
        }
        // Read second BAM sector (tracks 41-80)
        if (!read_sector(drive, D81_DIR_TRACK, 2, drive->bam2)) {
            f_close(&drive->file);
            drive->file_open = false;
            return false;
        }
        drive->bam2_dirty = false;
    } else {
        // D64/X64: BAM is at track 18, sector 0
        if (!read_sector(drive, DIR_TRACK, 0, drive->bam)) {
            f_close(&drive->file);
            drive->file_open = false;
            return false;
        }
    }
    drive->bam_dirty = false;

    drive->ready = true;
    d64_drive_set_error(drive, ERR_OK, 0, 0);

    MII_DEBUG_PRINTF("D64: Mounted OK, %d tracks, type=%d\n", drive->desc.num_tracks, drive->desc.type);

    return true;
}

/*
 * Unmount disk image
 */
void d64_drive_unmount(iec_drive_t *drive)
{
    if (drive == NULL) return;

    if (drive->file_open) {
        close_all_channels(drive);

        // Write back BAM if dirty
        if (drive->desc.type == IMAGE_TYPE_D81) {
            if (drive->bam_dirty) {
                write_sector(drive, D81_DIR_TRACK, 1, drive->bam);
                drive->bam_dirty = false;
            }
            if (drive->bam2_dirty) {
                write_sector(drive, D81_DIR_TRACK, 2, drive->bam2);
                drive->bam2_dirty = false;
            }
        } else if (drive->bam_dirty) {
            write_sector(drive, DIR_TRACK, 0, drive->bam);
            drive->bam_dirty = false;
        }

        f_close(&drive->file);
        drive->file_open = false;
    }

    drive->ready = false;
    drive->image_path[0] = '\0';

    d64_drive_set_error(drive, ERR_NOTREADY, 0, 0);
}

/*
 * Check if mounted
 */
bool d64_drive_is_mounted(iec_drive_t *drive)
{
    if (drive == NULL) return false;
    return drive->file_open && drive->ready;
}

/*
 * Parse image file and fill in descriptor
 */
static bool parse_image_file(iec_drive_t *drive)
{
    FILINFO fno;
    FRESULT fr;
    uint8_t header[64];
    UINT br;

    // Get file size
    fr = f_stat(drive->image_path, &fno);
    if (fr != FR_OK) {
        return false;
    }
    uint32_t size = fno.fsize;

    // Read header
    f_lseek(&drive->file, 0);
    fr = f_read(&drive->file, header, 64, &br);
    if (fr != FR_OK || br < 64) {
        return false;
    }

    // Check for X64 format
    if (memcmp(header, "C\x15\x41\x64\x01\x02", 6) == 0) {
        drive->desc.type = IMAGE_TYPE_X64;
        drive->desc.header_size = 64;

        // Read number of tracks
        f_lseek(&drive->file, 7);
        uint8_t nt;
        f_read(&drive->file, &nt, 1, &br);
        drive->desc.num_tracks = nt;
        if (drive->desc.num_tracks < 35 || drive->desc.num_tracks > 40) {
            return false;
        }

        // X64 files have no error info
        memset(drive->desc.error_info, 1, sizeof(drive->desc.error_info));
        drive->desc.has_error_info = false;
    }
    // Check for D81 format by size
    else if (size == D81_SIZE || size == D81_SIZE_ERR) {
        drive->desc.type = IMAGE_TYPE_D81;
        drive->desc.header_size = 0;
        drive->desc.num_tracks = D81_NUM_TRACKS;

        // Read error info if present
        memset(drive->desc.error_info, 1, sizeof(drive->desc.error_info));
        if (size == D81_SIZE_ERR) {
            f_lseek(&drive->file, D81_SIZE);
            f_read(&drive->file, drive->desc.error_info, NUM_SECTORS_D81, &br);
            drive->desc.has_error_info = true;
        } else {
            drive->desc.has_error_info = false;
        }
    }
    // Check for D64 format by size
    else if (size == D64_SIZE_35 || size == D64_SIZE_35_ERR ||
             size == D64_SIZE_40 || size == D64_SIZE_40_ERR) {
        drive->desc.type = IMAGE_TYPE_D64;
        drive->desc.header_size = 0;

        // Determine number of tracks
        if (size == D64_SIZE_40 || size == D64_SIZE_40_ERR) {
            drive->desc.num_tracks = 40;
        } else {
            drive->desc.num_tracks = 35;
        }

        // Read error info if present
        memset(drive->desc.error_info, 1, sizeof(drive->desc.error_info));
        if (size == D64_SIZE_35_ERR) {
            f_lseek(&drive->file, D64_SIZE_35);
            f_read(&drive->file, drive->desc.error_info, NUM_SECTORS_35, &br);
            drive->desc.has_error_info = true;
        } else if (size == D64_SIZE_40_ERR) {
            f_lseek(&drive->file, D64_SIZE_40);
            f_read(&drive->file, drive->desc.error_info, NUM_SECTORS_40, &br);
            drive->desc.has_error_info = true;
        } else {
            drive->desc.has_error_info = false;
        }
    } else {
        MII_DEBUG_PRINTF("D64: Unknown file format (size=%lu)\n", (unsigned long)size);
        return false;
    }

    // Read disk ID from BAM (use appropriate directory track)
    int bam_track = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    uint32_t bam_offset = d64_offset_from_ts(&drive->desc, bam_track, 0);
    uint8_t bam_buf[256];
    f_lseek(&drive->file, bam_offset);
    fr = f_read(&drive->file, bam_buf, 256, &br);
    if (fr == FR_OK && br == 256) {
        if (drive->desc.type == IMAGE_TYPE_D81) {
            // D81: ID at offset 22-23 in header sector
            drive->desc.id1 = bam_buf[22];
            drive->desc.id2 = bam_buf[23];
        } else {
            drive->desc.id1 = bam_buf[BAM_DISK_ID];
            drive->desc.id2 = bam_buf[BAM_DISK_ID + 1];
        }
    }

    return true;
}

/*
 * Read sector from disk image
 */
static bool read_sector(iec_drive_t *drive, int track, int sector, uint8_t *buffer)
{
    if (!drive->file_open) {
        d64_drive_set_error(drive, ERR_NOTREADY, track, sector);
        return false;
    }

    uint32_t offset = d64_offset_from_ts(&drive->desc, track, sector);
    if (offset == 0xFFFFFFFF) {
        d64_drive_set_error(drive, ERR_ILLEGALTS, track, sector);
        return false;
    }

    FRESULT fr;
    UINT br;

    fr = f_lseek(&drive->file, offset);
    if (fr != FR_OK) {
        d64_drive_set_error(drive, ERR_READ22, track, sector);
        return false;
    }

    fr = f_read(&drive->file, buffer, 256, &br);
    if (fr != FR_OK || br != 256) {
        d64_drive_set_error(drive, ERR_READ22, track, sector);
        return false;
    }

    // Check error info
    if (drive->desc.has_error_info) {
        uint8_t ei = drive->desc.error_info[accum_num_sectors[track] + sector];
        iec_error_t err = d64_conv_error_info(ei);
        if (err != ERR_OK) {
            d64_drive_set_error(drive, err, track, sector);
            return false;
        }
    }

    return true;
}

/*
 * Write sector to disk image
 */
static bool write_sector(iec_drive_t *drive, int track, int sector, uint8_t *buffer)
{
    if (!drive->file_open) {
        d64_drive_set_error(drive, ERR_NOTREADY, track, sector);
        return false;
    }

    if (drive->write_protected) {
        d64_drive_set_error(drive, ERR_WRITEPROTECT, track, sector);
        return false;
    }

    uint32_t offset = d64_offset_from_ts(&drive->desc, track, sector);
    if (offset == 0xFFFFFFFF) {
        d64_drive_set_error(drive, ERR_ILLEGALTS, track, sector);
        return false;
    }

    FRESULT fr;
    UINT bw;

    fr = f_lseek(&drive->file, offset);
    if (fr != FR_OK) {
        d64_drive_set_error(drive, ERR_WRITE25, track, sector);
        return false;
    }

    fr = f_write(&drive->file, buffer, 256, &bw);
    if (fr != FR_OK || bw != 256) {
        d64_drive_set_error(drive, ERR_WRITE25, track, sector);
        return false;
    }

    return true;
}

/*
 * Allocate buffer
 */
static int alloc_buffer(iec_drive_t *drive, int want)
{
    if (want == -1) {
        // Find any free buffer
        for (int i = NUM_BUFFERS - 1; i >= 0; i--) {
            if (drive->buf_free[i]) {
                drive->buf_free[i] = false;
                return i;
            }
        }
        return -1;
    }

    if (want < NUM_BUFFERS && drive->buf_free[want]) {
        drive->buf_free[want] = false;
        return want;
    }
    return -1;
}

/*
 * Free buffer
 */
static void free_buffer(iec_drive_t *drive, int buf)
{
    if (buf >= 0 && buf < NUM_BUFFERS) {
        drive->buf_free[buf] = true;
    }
}

/*
 * Close all channels
 */
static void close_all_channels(iec_drive_t *drive)
{
    for (int i = 0; i < 15; i++) {
        d64_drive_close(drive, i);
    }
    d64_drive_close(drive, 16);
    d64_drive_close(drive, 17);
    drive->cmd_len = 0;
}

/*
 * Check if block is free in BAM
 */
static bool is_block_free(iec_drive_t *drive, int track, int sector)
{
    if (drive->desc.type == IMAGE_TYPE_D81) {
        // D81: 6 bytes per track, BAM starts at offset 16 in BAM sectors
        // Tracks 1-40 in BAM sector 1, tracks 41-80 in BAM sector 2
        uint8_t *bam_sector;
        int bam_track;
        if (track <= 40) {
            bam_sector = drive->bam;
            bam_track = track;
        } else {
            bam_sector = drive->bam2;
            bam_track = track - 40;
        }
        uint8_t *p = bam_sector + 16 + (bam_track - 1) * D81_BAM_ENTRY_SIZE;
        int byte_idx = sector / 8 + 1;  // Skip free count byte
        int bit = sector & 7;
        return (p[byte_idx] & (1 << bit)) != 0;
    }

    // D64/X64
    uint8_t *p = drive->bam + BAM_BITMAP + (track - 1) * 4;
    int byte_idx = sector / 8 + 1;
    int bit = sector & 7;
    return (p[byte_idx] & (1 << bit)) != 0;
}

/*
 * Get number of free blocks on track
 */
static int num_free_blocks(iec_drive_t *drive, int track)
{
    if (drive->desc.type == IMAGE_TYPE_D81) {
        // D81: free count is first byte of 6-byte entry
        uint8_t *bam_sector;
        int bam_track;
        if (track <= 40) {
            bam_sector = drive->bam;
            bam_track = track;
        } else {
            bam_sector = drive->bam2;
            bam_track = track - 40;
        }
        return bam_sector[16 + (bam_track - 1) * D81_BAM_ENTRY_SIZE];
    }

    // D64/X64
    return drive->bam[BAM_BITMAP + (track - 1) * 4];
}

/*
 * Allocate block in BAM
 */
static int alloc_block(iec_drive_t *drive, int track, int sector)
{
    if (drive->desc.type == IMAGE_TYPE_D81) {
        if (track < 1 || track > D81_NUM_TRACKS || sector < 0 || sector >= D81_SECTORS_PER_TRACK) {
            return ERR_ILLEGALTS;
        }

        uint8_t *bam_sector;
        int bam_track;
        bool *dirty_flag;
        if (track <= 40) {
            bam_sector = drive->bam;
            bam_track = track;
            dirty_flag = &drive->bam_dirty;
        } else {
            bam_sector = drive->bam2;
            bam_track = track - 40;
            dirty_flag = &drive->bam2_dirty;
        }

        uint8_t *p = bam_sector + 16 + (bam_track - 1) * D81_BAM_ENTRY_SIZE;
        int byte_idx = sector / 8 + 1;
        int bit = sector & 7;

        if (p[byte_idx] & (1 << bit)) {
            // Block is free, allocate it
            p[byte_idx] &= ~(1 << bit);
            p[0]--;
            *dirty_flag = true;
            return ERR_OK;
        }
        return ERR_NOBLOCK;
    }

    // D64/X64
    if (track < 1 || track > 35 || sector < 0 || sector >= num_sectors[track]) {
        return ERR_ILLEGALTS;
    }

    uint8_t *p = drive->bam + BAM_BITMAP + (track - 1) * 4;
    int byte_idx = sector / 8 + 1;
    int bit = sector & 7;

    if (p[byte_idx] & (1 << bit)) {
        // Block is free, allocate it
        p[byte_idx] &= ~(1 << bit);
        p[0]--;
        drive->bam_dirty = true;
        return ERR_OK;
    }
    return ERR_NOBLOCK;
}

/*
 * Free block in BAM
 */
static int free_block(iec_drive_t *drive, int track, int sector)
{
    if (drive->desc.type == IMAGE_TYPE_D81) {
        if (track < 1 || track > D81_NUM_TRACKS || sector < 0 || sector >= D81_SECTORS_PER_TRACK) {
            return ERR_ILLEGALTS;
        }

        uint8_t *bam_sector;
        int bam_track;
        bool *dirty_flag;
        if (track <= 40) {
            bam_sector = drive->bam;
            bam_track = track;
            dirty_flag = &drive->bam_dirty;
        } else {
            bam_sector = drive->bam2;
            bam_track = track - 40;
            dirty_flag = &drive->bam2_dirty;
        }

        uint8_t *p = bam_sector + 16 + (bam_track - 1) * D81_BAM_ENTRY_SIZE;
        int byte_idx = sector / 8 + 1;
        int bit = sector & 7;

        if (!(p[byte_idx] & (1 << bit))) {
            // Block is allocated, free it
            p[byte_idx] |= (1 << bit);
            p[0]++;
            *dirty_flag = true;
        }
        return ERR_OK;
    }

    // D64/X64
    if (track < 1 || track > 35 || sector < 0 || sector >= num_sectors[track]) {
        return ERR_ILLEGALTS;
    }

    uint8_t *p = drive->bam + BAM_BITMAP + (track - 1) * 4;
    int byte_idx = sector / 8 + 1;
    int bit = sector & 7;

    if (!(p[byte_idx] & (1 << bit))) {
        // Block is allocated, free it
        p[byte_idx] |= (1 << bit);
        p[0]++;
        drive->bam_dirty = true;
    }
    return ERR_OK;
}

/*
 * Free chain of blocks
 */
static bool free_block_chain(iec_drive_t *drive, int track, int sector)
{
    uint8_t buf[256];
    while (free_block(drive, track, sector) == ERR_OK) {
        if (!read_sector(drive, track, sector, buf)) {
            return false;
        }
        track = buf[0];
        sector = buf[1];
    }
    return true;
}

/*
 * Allocate next block
 */
static bool alloc_next_block(iec_drive_t *drive, int *track, int *sector, int interleave)
{
    bool side_changed = false;
    int t = *track;
    int s = *sector;

    // Get disk geometry based on type
    int dir_track = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    int max_track = (drive->desc.type == IMAGE_TYPE_D81) ? D81_NUM_TRACKS : 35;

    // Find track with free blocks
    while (num_free_blocks(drive, t) == 0) {
        if (t == dir_track) {
            // Directory doesn't grow to other tracks
            d64_drive_set_error(drive, ERR_DISKFULL, 0, 0);
            return false;
        } else if (t > dir_track) {
            t++;
            if (t > max_track) {
                if (side_changed) {
                    d64_drive_set_error(drive, ERR_DISKFULL, 0, 0);
                    return false;
                }
                side_changed = true;
                t = dir_track - 1;
                s = 0;
            }
        } else {
            t--;
            if (t < 1) {
                if (side_changed) {
                    d64_drive_set_error(drive, ERR_DISKFULL, 0, 0);
                    return false;
                }
                side_changed = true;
                t = dir_track + 1;
                s = 0;
            }
        }
    }

    // Find next free block on track
    int num = (drive->desc.type == IMAGE_TYPE_D81) ? D81_SECTORS_PER_TRACK : num_sectors[t];
    s = s + interleave;
    if (s >= num) {
        s -= num;
        if (s) s--;
    }

    // Search for free block
    int count = 0;
    while (!is_block_free(drive, t, s) && count < num) {
        s++;
        if (s >= num) s = 0;
        count++;
    }

    if (count >= num) {
        // BAM inconsistency
        d64_drive_set_error(drive, ERR_DIRERROR, t, s);
        return false;
    }

    alloc_block(drive, t, s);
    *track = t;
    *sector = s;
    return true;
}

/*
 * Match pattern against name
 */
static bool match(const uint8_t *p, int p_len, const uint8_t *n)
{
    if (p_len > 16) p_len = 16;

    int c = 0;
    while (p_len-- > 0) {
        if (*p == '*') return true;  // Wildcard matches all
        if ((*p != *n) && (*p != '?')) return false;
        p++; n++; c++;
    }
    return *n == 0xa0 || c == 16;
}

/*
 * Find file in directory
 */
static bool find_file(iec_drive_t *drive, const uint8_t *pattern, int pattern_len,
                      int *dir_track, int *dir_sector, int *entry, bool cont)
{
    uint8_t *de = NULL;
    int num_dir_blocks = 0;

    // Get directory parameters based on disk type
    int first_dir_track = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    int first_dir_sector = (drive->desc.type == IMAGE_TYPE_D81) ? 3 : 1;
    int max_dir_sectors = (drive->desc.type == IMAGE_TYPE_D81) ? D81_SECTORS_PER_TRACK : num_sectors[DIR_TRACK];

    if (cont) {
        de = drive->dir + 2 + (*entry) * SIZEOF_DE;
    } else {
        drive->dir[0] = first_dir_track;
        drive->dir[1] = first_dir_sector;
        *entry = 8;
    }

    while (num_dir_blocks < max_dir_sectors) {
        (*entry)++;
        if (de) de += SIZEOF_DE;

        if (*entry >= 8) {
            // Read next directory block
            if (drive->dir[0] == 0) {
                return false;
            }

            // Save current track/sector BEFORE reading (as dir[0]/dir[1] will be overwritten)
            *dir_track = drive->dir[0];
            *dir_sector = drive->dir[1];

            if (!read_sector(drive, *dir_track, *dir_sector, drive->dir)) {
                return false;
            }

            num_dir_blocks++;
            *entry = 0;
            de = drive->dir + 2;  // DIR_ENTRIES start at offset 2
        }

        // Does entry match pattern?
        uint8_t file_type = de[DE_TYPE];
        if ((file_type & 0x3f) != FTYPE_DEL && match(pattern, pattern_len, de + DE_NAME)) {
            return true;
        }
    }
    return false;
}

static bool find_first_file(iec_drive_t *drive, const uint8_t *pattern, int pattern_len,
                           int *dir_track, int *dir_sector, int *entry)
{
    return find_file(drive, pattern, pattern_len, dir_track, dir_sector, entry, false);
}

static bool find_next_file(iec_drive_t *drive, const uint8_t *pattern, int pattern_len,
                          int *dir_track, int *dir_sector, int *entry)
{
    return find_file(drive, pattern, pattern_len, dir_track, dir_sector, entry, true);
}

/*
 * Allocate directory entry
 */
static bool alloc_dir_entry(iec_drive_t *drive, int *track, int *sector, int *entry)
{
    // Look for free entry in existing directory blocks
    // D81: directory starts at track 40, sector 3
    // D64: directory starts at track 18, sector 1
    drive->dir[0] = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    drive->dir[1] = (drive->desc.type == IMAGE_TYPE_D81) ? 3 : 1;

    while (drive->dir[0]) {
        if (!read_sector(drive, *track = drive->dir[0], *sector = drive->dir[1], drive->dir)) {
            return false;
        }

        uint8_t *de = drive->dir + 2;
        for (*entry = 0; *entry < 8; (*entry)++, de += SIZEOF_DE) {
            if (de[DE_TYPE] == 0) {
                return true;
            }
        }
    }

    // No free entry found, allocate new directory block
    int last_track = *track, last_sector = *sector;
    int dir_interleave = (drive->desc.type == IMAGE_TYPE_D81) ? D81_INTERLEAVE : DIR_INTERLEAVE;
    if (!alloc_next_block(drive, track, sector, dir_interleave)) {
        return false;
    }

    // Write link to new block
    drive->dir[0] = *track;
    drive->dir[1] = *sector;
    write_sector(drive, last_track, last_sector, drive->dir);

    // Write new empty directory block
    memset(drive->dir, 0, 256);
    drive->dir[1] = 0xff;
    write_sector(drive, *track, *sector, drive->dir);
    *entry = 0;
    return true;
}

/*
 * Open file for reading at track/sector
 */
static uint8_t open_file_ts(iec_drive_t *drive, int channel, int track, int sector)
{
    int buf = alloc_buffer(drive, -1);
    if (buf == -1) {
        d64_drive_set_error(drive, ERR_NOCHANNEL, 0, 0);
        return ST_OK;
    }

    drive->ch[channel].buf_num = buf;
    drive->ch[channel].buf = drive->ram + 0x300 + buf * 0x100;
    drive->ch[channel].mode = CHMOD_FILE;

    // Set up for first read
    drive->ch[channel].buf[0] = track;
    drive->ch[channel].buf[1] = sector;
    drive->ch[channel].buf_len = 0;

    return ST_OK;
}

/*
 * Create file for writing
 */
static uint8_t create_file(iec_drive_t *drive, int channel, const uint8_t *name, int name_len,
                          iec_ftype_t type, bool overwrite)
{
    int buf = alloc_buffer(drive, -1);
    if (buf == -1) {
        d64_drive_set_error(drive, ERR_NOCHANNEL, 0, 0);
        return ST_OK;
    }

    drive->ch[channel].buf_num = buf;
    drive->ch[channel].buf = drive->ram + 0x300 + buf * 0x100;

    // Allocate directory entry if not overwriting
    if (!overwrite) {
        if (!alloc_dir_entry(drive, &drive->ch[channel].dir_track,
                            &drive->ch[channel].dir_sector,
                            &drive->ch[channel].entry)) {
            free_buffer(drive, buf);
            return ST_OK;
        }
    }

    uint8_t *de = drive->dir + 2 + drive->ch[channel].entry * SIZEOF_DE;

    // Allocate first data block
    // D81: directory track is 40, interleave is 1
    // D64: directory track is 18, interleave is 10
    int dir_track_local = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    int data_interleave = (drive->desc.type == IMAGE_TYPE_D81) ? D81_INTERLEAVE : DATA_INTERLEAVE;
    drive->ch[channel].track = dir_track_local - 1;
    drive->ch[channel].sector = -data_interleave;
    if (!alloc_next_block(drive, &drive->ch[channel].track, &drive->ch[channel].sector, data_interleave)) {
        free_buffer(drive, buf);
        return ST_OK;
    }
    drive->ch[channel].num_blocks = 1;

    // Write directory entry
    memset(de, 0, SIZEOF_DE);
    de[DE_TYPE] = type;  // bit 7 not set -> open file
    if (overwrite) {
        de[DE_OVR_TRACK] = drive->ch[channel].track;
        de[DE_OVR_SECTOR] = drive->ch[channel].sector;
    } else {
        de[DE_TRACK] = drive->ch[channel].track;
        de[DE_SECTOR] = drive->ch[channel].sector;
    }
    memset(de + DE_NAME, 0xa0, 16);
    memcpy(de + DE_NAME, name, name_len > 16 ? 16 : name_len);
    write_sector(drive, drive->ch[channel].dir_track, drive->ch[channel].dir_sector, drive->dir);

    // Set channel descriptor
    drive->ch[channel].mode = CHMOD_FILE;
    drive->ch[channel].writing = true;
    drive->ch[channel].buf_ptr = drive->ch[channel].buf + 2;
    drive->ch[channel].buf_len = 2;

    return ST_OK;
}

/*
 * Open directory listing
 */
static uint8_t open_directory(iec_drive_t *drive, const uint8_t *pattern, int pattern_len)
{
    // Handle special "$0"
    if (pattern_len == 1 && pattern[0] == '0') {
        pattern++;
        pattern_len--;
    }

    // Look for pattern after ':'
    const uint8_t *t = (const uint8_t *)memchr(pattern, ':', pattern_len);
    if (t) {
        t++;
        pattern_len -= t - pattern;
        pattern = t;
    } else {
        pattern = (const uint8_t *)"*";
        pattern_len = 1;
    }

    // Allocate directory buffer (8KB should be enough)
    if (drive->dir_buf == NULL) {
        drive->dir_buf = (uint8_t *)malloc(8192);
        if (drive->dir_buf == NULL) {
            d64_drive_set_error(drive, ERR_NOCHANNEL, 0, 0);
            return ST_OK;
        }
        drive->dir_buf_size = 8192;
    }

    drive->ch[0].mode = CHMOD_DIRECTORY;
    uint8_t *p = drive->ch[0].buf_ptr = drive->ch[0].buf = drive->dir_buf;

    // Create directory title with disk name, ID and format type
    *p++ = 0x01;  // Load address $0401
    *p++ = 0x04;
    *p++ = 0x01;  // Dummy line link
    *p++ = 0x01;
    *p++ = 0;     // Drive number as line number
    *p++ = 0;
    *p++ = 0x12;  // RVS ON
    *p++ = '"';

    // Copy disk name - location differs for D81 vs D64
    uint8_t *q;
    uint8_t header_buf[256];
    if (drive->desc.type == IMAGE_TYPE_D81) {
        // D81: disk name is in header sector (track 40, sector 0) at offset 4
        read_sector(drive, D81_DIR_TRACK, 0, header_buf);
        q = header_buf + 4;
    } else {
        // D64: disk name is in BAM
        q = drive->bam + BAM_DISK_NAME;
    }
    for (int i = 0; i < 23; i++) {
        int c = *q++;
        if (c == 0xa0) {
            *p++ = ' ';
        } else {
            *p++ = c;
        }
    }
    *(p-7) = '"';
    *p++ = 0;

    // Scan directory blocks
    uint8_t dir_buf[256];
    int first_dir_track = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    int first_dir_sector = (drive->desc.type == IMAGE_TYPE_D81) ? 3 : 1;
    int max_dir_sectors = (drive->desc.type == IMAGE_TYPE_D81) ? D81_SECTORS_PER_TRACK : num_sectors[DIR_TRACK];
    dir_buf[0] = first_dir_track;
    dir_buf[1] = first_dir_sector;

    int num_dir_blocks = 0;
    while (dir_buf[0] && num_dir_blocks < max_dir_sectors) {
        if (!read_sector(drive, dir_buf[0], dir_buf[1], dir_buf)) {
            break;
        }
        num_dir_blocks++;

        // Scan 8 entries
        uint8_t *de = dir_buf + 2;
        for (int j = 0; j < 8; j++, de += SIZEOF_DE) {
            if (de[DE_TYPE] && match(pattern, pattern_len, de + DE_NAME)) {
                // Dummy line link
                *p++ = 0x01;
                *p++ = 0x01;

                // Line number = number of blocks
                *p++ = de[DE_NUM_BLOCKS_L];
                *p++ = de[DE_NUM_BLOCKS_H];

                // Spaces for alignment
                *p++ = ' ';
                int n = (de[DE_NUM_BLOCKS_H] << 8) + de[DE_NUM_BLOCKS_L];
                if (n < 10) *p++ = ' ';
                if (n < 100) *p++ = ' ';

                // File name in quotes
                *p++ = '"';
                q = de + DE_NAME;
                bool m = false;
                for (int i = 0; i < 16; i++) {
                    uint8_t c = *q++;
                    if (c == 0xa0) {
                        if (m) {
                            *p++ = ' ';
                        } else {
                            m = (*p++ = '"');
                        }
                    } else {
                        *p++ = c;
                    }
                }
                if (m) {
                    *p++ = ' ';
                } else {
                    *p++ = '"';
                }

                // Open files marked with '*'
                *p++ = (de[DE_TYPE] & 0x80) ? ' ' : '*';

                // File type
                *p++ = type_char_1[de[DE_TYPE] & 7];
                *p++ = type_char_2[de[DE_TYPE] & 7];
                *p++ = type_char_3[de[DE_TYPE] & 7];

                // Protected files marked with '<'
                *p++ = (de[DE_TYPE] & 0x40) ? '<' : ' ';

                // Trailing spaces
                *p++ = ' ';
                if (n >= 10) *p++ = ' ';
                if (n >= 100) *p++ = ' ';
                *p++ = 0;
            }
        }
    }

    // Final line: free blocks
    int n = 0;
    int max_track = (drive->desc.type == IMAGE_TYPE_D81) ? D81_NUM_TRACKS : 35;
    int dir_track_num = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
    for (int track = 1; track <= max_track; track++) {
        if (track != dir_track_num) {
            n += num_free_blocks(drive, track);
        }
    }

    *p++ = 0x01;
    *p++ = 0x01;
    *p++ = n & 0xff;
    *p++ = (n >> 8) & 0xff;

    memcpy(p, "BLOCKS FREE.", 12);
    p += 12;

    memset(p, ' ', 13);
    p += 13;

    *p++ = 0;
    *p++ = 0;
    *p++ = 0;

    drive->ch[0].buf_len = p - drive->ch[0].buf;
    return ST_OK;
}

/*
 * Open direct access channel
 */
static uint8_t open_direct(iec_drive_t *drive, int channel, const uint8_t *name)
{
    int buf = -1;

    if (name[1] == 0) {
        buf = alloc_buffer(drive, -1);
    } else if (name[1] >= '0' && name[1] <= '3' && name[2] == 0) {
        buf = alloc_buffer(drive, name[1] - '0');
    }

    if (buf == -1) {
        d64_drive_set_error(drive, ERR_NOCHANNEL, 0, 0);
        return ST_OK;
    }

    drive->ch[channel].mode = CHMOD_DIRECT;
    drive->ch[channel].buf = drive->ram + 0x300 + buf * 0x100;
    drive->ch[channel].buf_num = buf;

    // Store buffer number in buffer
    drive->ch[channel].buf[1] = buf + '0';
    drive->ch[channel].buf_len = 1;
    drive->ch[channel].buf_ptr = drive->ch[channel].buf + 1;

    return ST_OK;
}

/*
 * Parse file name
 */
void d64_parse_file_name(const uint8_t *src, int src_len, uint8_t *dest, int *dest_len,
                         iec_fmode_t *mode, iec_ftype_t *type, int *rec_len)
{
    // Look for ':' in name
    const uint8_t *p = (const uint8_t *)memchr(src, ':', src_len);
    if (p) {
        p++;
        src_len -= p - src;
    } else {
        p = src;
    }

    // Transfer name up to ','
    *dest_len = 0;
    uint8_t *q = dest;
    while (*p != ',' && src_len-- > 0) {
        *q++ = *p++;
        (*dest_len)++;
    }
    *q = 0;

    // Strip trailing CRs
    while (*dest_len > 0 && dest[*dest_len - 1] == 0x0d) {
        dest[--(*dest_len)] = 0;
    }

    // Look for mode and type parameters
    p++; src_len--;
    while (src_len > 0) {
        switch (*p) {
            case 'D': *type = FTYPE_DEL; break;
            case 'S': *type = FTYPE_SEQ; break;
            case 'P': *type = FTYPE_PRG; break;
            case 'U': *type = FTYPE_USR; break;
            case 'L':
                *type = FTYPE_REL;
                while (*p != ',' && src_len-- > 0) p++;
                p++; src_len--;
                *rec_len = *p++; src_len--;
                if (src_len < 0) *rec_len = 0;
                break;
            case 'R': *mode = FMODE_READ; break;
            case 'W': *mode = FMODE_WRITE; break;
            case 'A': *mode = FMODE_APPEND; break;
            case 'M': *mode = FMODE_M; break;
        }
        // Skip to ','
        while (*p != ',' && src_len-- > 0) p++;
        p++; src_len--;
    }
}

/*
 * Open file
 */
static uint8_t open_file(iec_drive_t *drive, int channel, const uint8_t *name, int name_len)
{
    uint8_t plain_name[IEC_NAMEBUF_LENGTH];
    int plain_name_len;
    iec_fmode_t mode = FMODE_READ;
    iec_ftype_t type = FTYPE_DEL;
    int rec_len = 0;

    d64_parse_file_name(name, name_len, plain_name, &plain_name_len, &mode, &type, &rec_len);
    if (plain_name_len > 16) plain_name_len = 16;

    // Channel 0 is READ, channel 1 is WRITE
    if (channel == 0 || channel == 1) {
        mode = channel ? FMODE_WRITE : FMODE_READ;
        if (type == FTYPE_DEL) type = FTYPE_PRG;
    }

    drive->ch[channel].writing = (mode == FMODE_WRITE || mode == FMODE_APPEND);

    // Wildcards only on reading
    if (drive->ch[channel].writing &&
        (memchr(plain_name, '*', plain_name_len) || memchr(plain_name, '?', plain_name_len))) {
        d64_drive_set_error(drive, ERR_SYNTAX33, 0, 0);
        return ST_OK;
    }

    // Check write protection
    if (drive->ch[channel].writing && drive->write_protected) {
        d64_drive_set_error(drive, ERR_WRITEPROTECT, 0, 0);
        return ST_OK;
    }

    // Relative files not supported
    if (type == FTYPE_REL) {
        d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
        return ST_OK;
    }

    // Find file in directory
    int dir_track, dir_sector, entry;
    if (find_first_file(drive, plain_name, plain_name_len, &dir_track, &dir_sector, &entry)) {
        // File exists
        drive->ch[channel].dir_track = dir_track;
        drive->ch[channel].dir_sector = dir_sector;
        drive->ch[channel].entry = entry;
        uint8_t *de = drive->dir + 2 + entry * SIZEOF_DE;

        // Get file type from existing file
        if (type == FTYPE_DEL) {
            type = de[DE_TYPE] & 7;
        }

        if ((de[DE_TYPE] & 7) != type) {
            d64_drive_set_error(drive, ERR_FILETYPE, 0, 0);
        } else if (mode == FMODE_WRITE) {
            if (name[0] == '@') {
                // Save-replace
                return create_file(drive, channel, plain_name, plain_name_len, type, true);
            } else {
                d64_drive_set_error(drive, ERR_FILEEXISTS, 0, 0);
            }
        } else if (mode == FMODE_APPEND) {
            // Open for appending
            open_file_ts(drive, channel, de[DE_TRACK], de[DE_SECTOR]);

            // Seek to end
            int track = 0, sector = 0, num_blocks = 0;
            while (drive->ch[channel].buf[0]) {
                if (!read_sector(drive, track = drive->ch[channel].buf[0],
                               sector = drive->ch[channel].buf[1], drive->ch[channel].buf)) {
                    return ST_OK;
                }
                num_blocks++;
            }

            drive->ch[channel].writing = true;
            drive->ch[channel].buf_len = drive->ch[channel].buf[1] + 1;
            drive->ch[channel].buf_ptr = drive->ch[channel].buf + drive->ch[channel].buf_len;
            drive->ch[channel].track = track;
            drive->ch[channel].sector = sector;
            drive->ch[channel].num_blocks = num_blocks;
        } else if (mode == FMODE_M) {
            // Open even if not closed
            return open_file_ts(drive, channel, de[DE_TRACK], de[DE_SECTOR]);
        } else {
            // Normal read
            if (de[DE_TYPE] & 0x80) {
                return open_file_ts(drive, channel, de[DE_TRACK], de[DE_SECTOR]);
            } else {
                d64_drive_set_error(drive, ERR_WRITEFILEOPEN, 0, 0);
            }
        }
    } else {
        // File not found
        if (type == FTYPE_DEL) type = FTYPE_SEQ;

        if (mode == FMODE_WRITE) {
            return create_file(drive, channel, plain_name, plain_name_len, type, false);
        } else {
            d64_drive_set_error(drive, ERR_FILENOTFOUND, 0, 0);
        }
    }
    return ST_OK;
}

/*
 * Open channel
 */
uint8_t d64_drive_open(iec_drive_t *drive, int channel, const uint8_t *name, int name_len)
{
    d64_drive_set_error(drive, ERR_OK, 0, 0);

    // Channel 15: execute command
    if (channel == 15) {
        d64_execute_cmd(drive, name, name_len);
        return ST_OK;
    }

    if (drive->ch[channel].mode != CHMOD_FREE) {
        d64_drive_set_error(drive, ERR_NOCHANNEL, 0, 0);
        return ST_OK;
    }

    if (name[0] == '$') {
        if (channel) {
            // Open raw directory track/sector
            int dir_track_raw = (drive->desc.type == IMAGE_TYPE_D81) ? D81_DIR_TRACK : DIR_TRACK;
            return open_file_ts(drive, channel, dir_track_raw, 0);
        } else {
            return open_directory(drive, name + 1, name_len - 1);
        }
    }

    if (name[0] == '#') {
        return open_direct(drive, channel, name);
    }

    return open_file(drive, channel, name, name_len);
}

/*
 * Close channel
 */
uint8_t d64_drive_close(iec_drive_t *drive, int channel)
{
    switch (drive->ch[channel].mode) {
        case CHMOD_FREE:
            break;

        case CHMOD_COMMAND:
            close_all_channels(drive);
            break;

        case CHMOD_DIRECT:
            free_buffer(drive, drive->ch[channel].buf_num);
            drive->ch[channel].buf = NULL;
            drive->ch[channel].mode = CHMOD_FREE;
            break;

        case CHMOD_FILE:
            if (drive->ch[channel].writing) {
                // Write last block
                if (drive->ch[channel].buf_len == 2) {
                    drive->ch[channel].buf[2] = 0x0d;  // CR
                    drive->ch[channel].buf_len++;
                }

                drive->ch[channel].buf[0] = 0;
                drive->ch[channel].buf[1] = drive->ch[channel].buf_len - 1;
                if (!write_sector(drive, drive->ch[channel].track,
                                drive->ch[channel].sector, drive->ch[channel].buf)) {
                    goto free_buf;
                }

                // Update directory entry
                read_sector(drive, drive->ch[channel].dir_track,
                           drive->ch[channel].dir_sector, drive->dir);
                uint8_t *de = drive->dir + 2 + drive->ch[channel].entry * SIZEOF_DE;
                de[DE_TYPE] |= 0x80;  // Close file
                de[DE_NUM_BLOCKS_L] = drive->ch[channel].num_blocks & 0xff;
                de[DE_NUM_BLOCKS_H] = drive->ch[channel].num_blocks >> 8;

                if (de[DE_OVR_TRACK]) {
                    // Overwriting, free old blocks
                    free_block_chain(drive, de[DE_TRACK], de[DE_SECTOR]);
                    de[DE_TRACK] = de[DE_OVR_TRACK];
                    de[DE_SECTOR] = de[DE_OVR_SECTOR];
                    de[DE_OVR_TRACK] = de[DE_OVR_SECTOR] = 0;
                }
                write_sector(drive, drive->ch[channel].dir_track,
                           drive->ch[channel].dir_sector, drive->dir);
            }
free_buf:
            free_buffer(drive, drive->ch[channel].buf_num);
            drive->ch[channel].buf = NULL;
            drive->ch[channel].mode = CHMOD_FREE;
            break;

        case CHMOD_DIRECTORY:
            // dir_buf is reused, don't free
            drive->ch[channel].buf = NULL;
            drive->ch[channel].mode = CHMOD_FREE;
            break;

        default:
            break;
    }
    return ST_OK;
}

/*
 * Read byte from channel
 */
uint8_t d64_drive_read(iec_drive_t *drive, int channel, uint8_t *byte)
{
    switch (drive->ch[channel].mode) {
        case CHMOD_FREE:
            if (drive->current_error == ERR_OK) {
                d64_drive_set_error(drive, ERR_FILENOTOPEN, 0, 0);
            }
            break;

        case CHMOD_COMMAND:
            // Read error channel
            *byte = *drive->error_ptr++;
            if (--drive->error_len) {
                return ST_OK;
            } else {
                d64_drive_set_error(drive, ERR_OK, 0, 0);
                return ST_EOF;
            }

        case CHMOD_FILE:
            if (drive->ch[channel].writing) return ST_READ_TIMEOUT;
            if (drive->current_error != ERR_OK) return ST_READ_TIMEOUT;

            // Read next block if necessary
            if (drive->ch[channel].buf_len == 0 && drive->ch[channel].buf[0]) {
                if (!read_sector(drive, drive->ch[channel].buf[0],
                               drive->ch[channel].buf[1], drive->ch[channel].buf)) {
                    return ST_READ_TIMEOUT;
                }
                drive->ch[channel].buf_ptr = drive->ch[channel].buf + 2;
                drive->ch[channel].buf_len = drive->ch[channel].buf[0] ? 254 :
                                            drive->ch[channel].buf[1] - 1;
            }

            if (drive->ch[channel].buf_len > 0) {
                *byte = *(drive->ch[channel].buf_ptr)++;
                if (--drive->ch[channel].buf_len == 0 && drive->ch[channel].buf[0] == 0) {
                    return ST_EOF;
                } else {
                    return ST_OK;
                }
            }
            return ST_READ_TIMEOUT;

        case CHMOD_DIRECTORY:
        case CHMOD_DIRECT:
            if (drive->ch[channel].buf_len > 0) {
                *byte = *(drive->ch[channel].buf_ptr)++;
                if (--drive->ch[channel].buf_len) {
                    return ST_OK;
                } else {
                    return ST_EOF;
                }
            }
            return ST_READ_TIMEOUT;

        default:
            break;
    }
    return ST_READ_TIMEOUT;
}

/*
 * Write byte to channel
 */
uint8_t d64_drive_write(iec_drive_t *drive, int channel, uint8_t byte, bool eoi)
{
    switch (drive->ch[channel].mode) {
        case CHMOD_FREE:
            if (drive->current_error == ERR_OK) {
                d64_drive_set_error(drive, ERR_FILENOTOPEN, 0, 0);
            }
            break;

        case CHMOD_COMMAND:
            // Collect command
            if (drive->cmd_len > 58) {
                d64_drive_set_error(drive, ERR_SYNTAX32, 0, 0);
                return ST_TIMEOUT;
            }
            drive->cmd_buf[drive->cmd_len++] = byte;

            if (eoi) {
                d64_execute_cmd(drive, drive->cmd_buf, drive->cmd_len);
                drive->cmd_len = 0;
            }
            return ST_OK;

        case CHMOD_DIRECTORY:
            d64_drive_set_error(drive, ERR_WRITEFILEOPEN, 0, 0);
            break;

        case CHMOD_FILE:
            if (!drive->ch[channel].writing) return ST_TIMEOUT;
            if (drive->current_error != ERR_OK) return ST_TIMEOUT;

            // Buffer full?
            if (drive->ch[channel].buf_len >= 256) {
                int track = drive->ch[channel].track;
                int sector = drive->ch[channel].sector;
                int file_interleave = (drive->desc.type == IMAGE_TYPE_D81) ? D81_INTERLEAVE : DATA_INTERLEAVE;
                if (!alloc_next_block(drive, &track, &sector, file_interleave)) {
                    return ST_TIMEOUT;
                }
                drive->ch[channel].num_blocks++;

                drive->ch[channel].buf[0] = track;
                drive->ch[channel].buf[1] = sector;
                write_sector(drive, drive->ch[channel].track,
                           drive->ch[channel].sector, drive->ch[channel].buf);

                drive->ch[channel].buf_ptr = drive->ch[channel].buf + 2;
                drive->ch[channel].buf_len = 2;
                drive->ch[channel].track = track;
                drive->ch[channel].sector = sector;
            }
            *(drive->ch[channel].buf_ptr)++ = byte;
            drive->ch[channel].buf_len++;
            return ST_OK;

        case CHMOD_DIRECT:
            if (drive->ch[channel].buf_len < 256) {
                *(drive->ch[channel].buf_ptr)++ = byte;
                drive->ch[channel].buf_len++;
                return ST_OK;
            }
            return ST_TIMEOUT;

        default:
            break;
    }
    return ST_TIMEOUT;
}

/*
 * Execute DOS command
 */
void d64_execute_cmd(iec_drive_t *drive, const uint8_t *cmd, int cmd_len)
{
    // Strip trailing CRs
    while (cmd_len > 0 && cmd[cmd_len - 1] == 0x0d) {
        cmd_len--;
    }

    d64_drive_set_error(drive, ERR_OK, 0, 0);

    // Find delimiters
    const uint8_t *colon = (const uint8_t *)memchr(cmd, ':', cmd_len);
    const uint8_t *minus = (const uint8_t *)memchr(cmd, '-', cmd_len);

    switch (cmd[0]) {
        case 'I':  // Initialize
            close_all_channels(drive);
            if (drive->desc.type == IMAGE_TYPE_D81) {
                if (drive->bam_dirty) {
                    write_sector(drive, D81_DIR_TRACK, 1, drive->bam);
                    drive->bam_dirty = false;
                }
                if (drive->bam2_dirty) {
                    write_sector(drive, D81_DIR_TRACK, 2, drive->bam2);
                    drive->bam2_dirty = false;
                }
                read_sector(drive, D81_DIR_TRACK, 1, drive->bam);
                read_sector(drive, D81_DIR_TRACK, 2, drive->bam2);
            } else {
                if (drive->bam_dirty) {
                    write_sector(drive, DIR_TRACK, 0, drive->bam);
                    drive->bam_dirty = false;
                }
                read_sector(drive, DIR_TRACK, 0, drive->bam);
            }
            break;

        case 'U':  // User commands
            if (cmd[1] == '0') break;
            switch (cmd[1] & 0x0f) {
                case 9:   // UI: Mode switch
                case 10:  // UJ: Reset
                    d64_drive_reset(drive);
                    break;
                default:
                    d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
                    break;
            }
            break;

        case 'B':  // Block commands
            if (!minus) {
                d64_drive_set_error(drive, ERR_SYNTAX31, 0, 0);
            } else {
                // Parse B-R, B-W, etc.
                d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
            }
            break;

        case 'M':  // Memory commands
            d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
            break;

        case 'V':  // Validate
            d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
            break;

        case 'N':  // New (format)
            d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
            break;

        case 'S':  // Scratch
            if (!colon) {
                d64_drive_set_error(drive, ERR_SYNTAX34, 0, 0);
            } else {
                // Simple scratch implementation
                const uint8_t *files = colon + 1;
                int files_len = cmd_len - (colon + 1 - cmd);

                if (drive->write_protected) {
                    d64_drive_set_error(drive, ERR_WRITEPROTECT, 0, 0);
                    break;
                }

                int num_files = 0;
                int dir_track, dir_sector, entry;

                if (find_first_file(drive, files, files_len, &dir_track, &dir_sector, &entry)) {
                    do {
                        uint8_t *de = drive->dir + 2 + entry * SIZEOF_DE;
                        if (de[DE_TYPE] & 0x40) continue;  // Protected

                        free_block_chain(drive, de[DE_TRACK], de[DE_SECTOR]);
                        free_block_chain(drive, de[DE_SIDE_TRACK], de[DE_SIDE_SECTOR]);
                        de[DE_TYPE] = 0;
                        write_sector(drive, dir_track, dir_sector, drive->dir);
                        num_files++;
                    } while (find_next_file(drive, files, files_len, &dir_track, &dir_sector, &entry));
                }
                d64_drive_set_error(drive, ERR_SCRATCHED, num_files, 0);
            }
            break;

        case 'R':  // Rename
        case 'C':  // Copy
            d64_drive_set_error(drive, ERR_UNIMPLEMENTED, 0, 0);
            break;

        default:
            d64_drive_set_error(drive, ERR_SYNTAX31, 0, 0);
            break;
    }
}

/*
 * Check if file is a valid D64/X64 image
 */
bool d64_is_disk_image(const char *path, const uint8_t *header, uint32_t size)
{
    // Check for X64
    if (memcmp(header, "C\x15\x41\x64\x01\x02", 6) == 0) {
        return true;
    }

    // Check for D81 by size
    if (size == D81_SIZE || size == D81_SIZE_ERR) {
        return true;
    }

    // Check for D64 by size
    return size == D64_SIZE_35 || size == D64_SIZE_35_ERR ||
           size == D64_SIZE_40 || size == D64_SIZE_40_ERR;
}
