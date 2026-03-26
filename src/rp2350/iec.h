/*
 *  iec.h - IEC bus routines, 1541 emulation (DOS level)
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

#ifndef IEC_DOS_H
#define IEC_DOS_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Constants
 */

// Maximum length of file names
#define IEC_NAMEBUF_LENGTH 256

// C64 status codes (returned to KERNAL)
#define ST_OK           0x00    // No error
#define ST_READ_TIMEOUT 0x02    // Timeout on reading
#define ST_TIMEOUT      0x03    // Timeout
#define ST_EOF          0x40    // End of file
#define ST_NOTPRESENT   0x80    // Device not present

// 1541 error codes
typedef enum {
    ERR_OK,             // 00 OK
    ERR_SCRATCHED,      // 01 FILES SCRATCHED
    ERR_UNIMPLEMENTED,  // 03 UNIMPLEMENTED
    ERR_READ20,         // 20 READ ERROR (block header not found)
    ERR_READ21,         // 21 READ ERROR (no sync character)
    ERR_READ22,         // 22 READ ERROR (data block not present)
    ERR_READ23,         // 23 READ ERROR (checksum error in data block)
    ERR_READ24,         // 24 READ ERROR (byte decoding error)
    ERR_WRITE25,        // 25 WRITE ERROR (write-verify error)
    ERR_WRITEPROTECT,   // 26 WRITE PROTECT ON
    ERR_READ27,         // 27 READ ERROR (checksum error in header)
    ERR_WRITE28,        // 28 WRITE ERROR (long data block)
    ERR_DISKID,         // 29 DISK ID MISMATCH
    ERR_SYNTAX30,       // 30 SYNTAX ERROR (general syntax)
    ERR_SYNTAX31,       // 31 SYNTAX ERROR (invalid command)
    ERR_SYNTAX32,       // 32 SYNTAX ERROR (command too long)
    ERR_SYNTAX33,       // 33 SYNTAX ERROR (wildcards on writing)
    ERR_SYNTAX34,       // 34 SYNTAX ERROR (missing file name)
    ERR_WRITEFILEOPEN,  // 60 WRITE FILE OPEN
    ERR_FILENOTOPEN,    // 61 FILE NOT OPEN
    ERR_FILENOTFOUND,   // 62 FILE NOT FOUND
    ERR_FILEEXISTS,     // 63 FILE EXISTS
    ERR_FILETYPE,       // 64 FILE TYPE MISMATCH
    ERR_NOBLOCK,        // 65 NO BLOCK
    ERR_ILLEGALTS,      // 66 ILLEGAL TRACK OR SECTOR
    ERR_NOCHANNEL,      // 70 NO CHANNEL
    ERR_DIRERROR,       // 71 DIR ERROR
    ERR_DISKFULL,       // 72 DISK FULL
    ERR_STARTUP,        // 73 Power-up message
    ERR_NOTREADY        // 74 DRIVE NOT READY
} iec_error_t;

// 1541 file types
typedef enum {
    FTYPE_DEL = 0,      // Deleted
    FTYPE_SEQ = 1,      // Sequential
    FTYPE_PRG = 2,      // Program
    FTYPE_USR = 3,      // User
    FTYPE_REL = 4,      // Relative
    FTYPE_UNKNOWN = 5
} iec_ftype_t;

// 1541 file access modes
typedef enum {
    FMODE_READ = 0,     // Read
    FMODE_WRITE = 1,    // Write
    FMODE_APPEND = 2,   // Append
    FMODE_M = 3         // Read open file
} iec_fmode_t;

// Drive LED states
typedef enum {
    DRVLED_OFF,         // Inactive, LED off
    DRVLED_ON,          // Active, LED on
    DRVLED_ERROR_OFF,   // Error, LED off
    DRVLED_ERROR_ON,    // Error, LED on
    DRVLED_ERROR_FLASH  // Error, flash LED
} iec_led_t;

// Channel modes
typedef enum {
    CHMOD_FREE = 0,     // Channel free
    CHMOD_COMMAND,      // Command/error channel
    CHMOD_DIRECTORY,    // Reading directory
    CHMOD_FILE,         // Sequential file open
    CHMOD_REL,          // Relative file open (not supported)
    CHMOD_DIRECT        // Direct buffer access (#)
} iec_chmod_t;

/*
 * Forward declarations
 */

typedef struct iec_drive iec_drive_t;

/*
 * IEC Bus structure - manages the single drive
 */

typedef struct {
    iec_drive_t *drive;         // Single drive (device 8)

    uint8_t name_buf[IEC_NAMEBUF_LENGTH];  // Buffer for file names
    uint8_t *name_ptr;          // Pointer for reception of file name
    int name_len;               // Received length of file name

    iec_drive_t *listener;      // Pointer to active listener
    iec_drive_t *talker;        // Pointer to active talker

    bool listener_active;       // Listener selected
    bool talker_active;         // Talker selected
    bool listening;             // Last ATN was listen

    uint8_t received_cmd;       // Received command code ($x0)
    uint8_t sec_addr;           // Received secondary address ($0x)
} iec_bus_t;

/*
 * IEC Bus API
 */

// Initialize IEC bus
void iec_init(void);

// Reset IEC bus and drive
void iec_reset(void);

// Get IEC bus instance
iec_bus_t *iec_get_bus(void);

// Output byte to listener (with EOI flag)
uint8_t iec_out(uint8_t byte, bool eoi);

// Output byte with ATN (Talk/Listen/Untalk/Unlisten)
uint8_t iec_out_atn(uint8_t byte);

// Output secondary address
uint8_t iec_out_sec(uint8_t byte);

// Read byte from talker
uint8_t iec_in(uint8_t *byte);

// ATN control (not needed for emulation)
void iec_set_atn(void);
void iec_rel_atn(void);
void iec_turnaround(void);
void iec_release(void);

// Update drive LED display
void iec_update_leds(void);

// Mount disk image on the drive
// Returns true on success
bool iec_mount_image(const char *path);

// Unmount current disk image
void iec_unmount_image(void);

// Check if disk is mounted
bool iec_is_mounted(void);

// Get drive LED state
iec_led_t iec_get_led_state(void);

// Get current error message
const char *iec_get_error_string(void);

/*
 * Charset conversion utilities
 */

// Convert ASCII character to PETSCII
uint8_t ascii_to_petscii(char c);

// Convert ASCII string to PETSCII string
void ascii_to_petscii_str(uint8_t *dest, const char *src, int max);

// Convert PETSCII character to ASCII
char petscii_to_ascii(uint8_t c);

// Convert PETSCII string to ASCII string
void petscii_to_ascii_str(char *dest, const uint8_t *src, int max);

#endif // IEC_DOS_H
