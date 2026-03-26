/*
 *  d64_drive.h - 1541 emulation in disk image files (.d64/.x64)
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
 */

#ifndef D64_DRIVE_H
#define D64_DRIVE_H

#include <stdint.h>
#include <stdbool.h>
#include "iec.h"
#include "fatfs/ff.h"

/*
 * Constants
 */

// Number of sectors in disk images
#define NUM_SECTORS_35  683     // 35-track image
#define NUM_SECTORS_40  768     // 40-track image
#define NUM_SECTORS_D81 3200    // D81 image (80 tracks * 40 sectors)

// D64 image sizes
#define D64_SIZE_35         (NUM_SECTORS_35 * 256)      // 174848
#define D64_SIZE_35_ERR     (NUM_SECTORS_35 * 257)      // 175531
#define D64_SIZE_40         (NUM_SECTORS_40 * 256)      // 196608
#define D64_SIZE_40_ERR     (NUM_SECTORS_40 * 257)      // 197376

// D81 image sizes
#define D81_SIZE            (NUM_SECTORS_D81 * 256)     // 819200
#define D81_SIZE_ERR        (NUM_SECTORS_D81 * 256 + NUM_SECTORS_D81) // 822400

// D81 constants
#define D81_SECTORS_PER_TRACK  40
#define D81_NUM_TRACKS         80
#define D81_DIR_TRACK          40
#define D81_INTERLEAVE         1
#define D81_BAM_ENTRY_SIZE     6    // 1 free count + 5 bytes for 40-bit bitmap

// Directory track (D64)
#define DIR_TRACK   18

// Maximum number of channels
#define MAX_CHANNELS    18      // 0-15 + internal read/write

// Number of work buffers
#define NUM_BUFFERS     4

// Disk image types
typedef enum {
    IMAGE_TYPE_D64 = 0,
    IMAGE_TYPE_X64 = 1,
    IMAGE_TYPE_D81 = 2
} image_type_t;

/*
 * BAM structure offsets
 */
#define BAM_DIR_TRACK   0       // Track of first directory block
#define BAM_DIR_SECTOR  1       // Sector of first directory block
#define BAM_FMT_TYPE    2       // Format type
#define BAM_BITMAP      4       // Sector allocation map
#define BAM_DISK_NAME   144     // Disk name (16 bytes)
#define BAM_DISK_ID     162     // Disk ID (2 bytes)
#define BAM_FMT_CHAR    165     // Format characters

/*
 * Directory entry structure offsets
 */
#define DE_TYPE             0   // File type/flags
#define DE_TRACK            1   // Track of first data block
#define DE_SECTOR           2   // Sector of first data block
#define DE_NAME             3   // File name (16 bytes)
#define DE_SIDE_TRACK       19  // Side sector track
#define DE_SIDE_SECTOR      20  // Side sector sector
#define DE_REC_LEN          21  // Record length
#define DE_OVR_TRACK        26  // Overwrite track
#define DE_OVR_SECTOR       27  // Overwrite sector
#define DE_NUM_BLOCKS_L     28  // Number of blocks (low byte)
#define DE_NUM_BLOCKS_H     29  // Number of blocks (high byte)
#define SIZEOF_DE           32  // Size of directory entry

/*
 * Channel descriptor
 */
typedef struct {
    iec_chmod_t mode;       // Channel mode
    bool writing;           // Flag: writing to file
    int buf_num;            // Buffer number for direct access
    uint8_t *buf;           // Pointer to start of buffer
    uint8_t *buf_ptr;       // Pointer to current position
    int buf_len;            // Remaining bytes in buffer
    int track;              // Track for write operations
    int sector;             // Sector for write operations
    int num_blocks;         // Number of blocks in file
    int dir_track;          // Directory block track
    int dir_sector;         // Directory block sector
    int entry;              // Entry number in directory block
} channel_desc_t;

/*
 * Image file descriptor
 */
typedef struct {
    image_type_t type;      // Image type
    int header_size;        // Size of file header (64 for X64, 0 for D64/D81)
    int num_tracks;         // Number of tracks (35, 40, or 80 for D81)
    uint8_t id1, id2;       // Block header ID
    uint8_t error_info[NUM_SECTORS_D81];  // Sector error info (sized for largest format)
    bool has_error_info;    // Flag: error info present
} image_file_desc_t;

/*
 * IEC Drive structure
 * Single drive instance for device 8
 */
struct iec_drive {
    // Drive state
    iec_led_t led;          // Drive LED state
    bool ready;             // Drive is ready for operation

    // Error handling
    char error_buf[256];    // Error message buffer
    char *error_ptr;        // Pointer within error message
    int error_len;          // Remaining length of error message
    iec_error_t current_error;  // Current error code

    // Command buffer
    uint8_t cmd_buf[64];    // Command buffer
    int cmd_len;            // Command length

    // Image file
    FIL file;               // FatFS file handle
    bool file_open;         // File is open
    image_file_desc_t desc; // Image file descriptor
    bool write_protected;   // Write protection flag
    char image_path[128];   // Path to current image

    // 1541 RAM emulation (2KB)
    uint8_t ram[0x800];     // 2KB RAM
    uint8_t dir[258];       // Directory block buffer (256 + 2 for safety)
    uint8_t *bam;           // Pointer to BAM in RAM (buffer 4, upper 256 bytes)
    bool bam_dirty;         // BAM modified flag
    uint8_t bam2[256];      // Second BAM sector for D81 (tracks 41-80)
    bool bam2_dirty;        // Second BAM modified flag (D81 only)

    // Channel descriptors
    channel_desc_t ch[MAX_CHANNELS];

    // Buffer allocation flags
    bool buf_free[NUM_BUFFERS];

    // Directory buffer for CHMOD_DIRECTORY mode
    // This is dynamically allocated when opening directory
    uint8_t *dir_buf;       // Directory listing buffer
    int dir_buf_size;       // Size of allocated buffer
};

/*
 * Drive API
 */

// Create and initialize drive
iec_drive_t *d64_drive_create(void);

// Destroy drive
void d64_drive_destroy(iec_drive_t *drive);

// Open channel
uint8_t d64_drive_open(iec_drive_t *drive, int channel, const uint8_t *name, int name_len);

// Close channel
uint8_t d64_drive_close(iec_drive_t *drive, int channel);

// Read byte from channel
uint8_t d64_drive_read(iec_drive_t *drive, int channel, uint8_t *byte);

// Write byte to channel
uint8_t d64_drive_write(iec_drive_t *drive, int channel, uint8_t byte, bool eoi);

// Reset drive
void d64_drive_reset(iec_drive_t *drive);

// Mount disk image
bool d64_drive_mount(iec_drive_t *drive, const char *path);

// Unmount disk image
void d64_drive_unmount(iec_drive_t *drive);

// Check if mounted
bool d64_drive_is_mounted(iec_drive_t *drive);

// Set error message
void d64_drive_set_error(iec_drive_t *drive, iec_error_t error, int track, int sector);

/*
 * Helper functions
 */

// Parse file name
void d64_parse_file_name(const uint8_t *src, int src_len, uint8_t *dest, int *dest_len,
                         iec_fmode_t *mode, iec_ftype_t *type, int *rec_len);

// Execute DOS command
void d64_execute_cmd(iec_drive_t *drive, const uint8_t *cmd, int cmd_len);

// Check if file is a valid D64/X64 image
bool d64_is_disk_image(const char *path, const uint8_t *header, uint32_t size);

// Get sectors per track
int d64_sectors_per_track(int track);

// Convert track/sector to byte offset
uint32_t d64_offset_from_ts(const image_file_desc_t *desc, int track, int sector);

// Convert D64 error info to error code
iec_error_t d64_conv_error_info(uint8_t error);

#endif // D64_DRIVE_H
