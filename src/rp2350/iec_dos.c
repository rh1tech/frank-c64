/*
 *  iec_dos.c - IEC bus routines, 1541 emulation (DOS level)
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

/*
 * Notes:
 * ------
 * - There are three kinds of devices on the IEC bus: controllers,
 *   listeners and talkers. We are always the controller and we
 *   can additionally be either listener or talker.
 * - This implementation supports a single drive (device 8) to save RAM.
 * - The Drive object has virtual functions for channel operations:
 *     Open() opens a channel
 *     Close() closes a channel
 *     Read() reads from a channel
 *     Write() writes to a channel
 * - The EOI/EOF signal is special: it is sent before the last byte, not after.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "iec.h"
#include "d64_drive.h"
#include "debug_log.h"

/*
 * IEC command codes
 */
#define CMD_DATA    0x60    // Data transfer
#define CMD_CLOSE   0xe0    // Close channel
#define CMD_OPEN    0xf0    // Open channel

/*
 * IEC ATN codes
 */
#define ATN_LISTEN      0x20
#define ATN_UNLISTEN    0x30
#define ATN_TALK        0x40
#define ATN_UNTALK      0x50

/*
 * Global IEC bus instance
 */
static iec_bus_t iec_bus;
static bool iec_initialized = false;

/*
 * Initialize IEC bus
 */
void iec_init(void)
{
    if (iec_initialized) {
        return;
    }

    memset(&iec_bus, 0, sizeof(iec_bus_t));

    // Create drive (device 8 only)
    iec_bus.drive = d64_drive_create();
    if (iec_bus.drive == NULL) {
        MII_DEBUG_PRINTF("IEC: Failed to create drive\n");
        return;
    }

    iec_bus.listener_active = false;
    iec_bus.talker_active = false;
    iec_bus.listening = false;

    iec_initialized = true;
    MII_DEBUG_PRINTF("IEC: DOS-level emulation initialized\n");
}

/*
 * Reset IEC bus and drive
 */
void iec_reset(void)
{
    if (!iec_initialized) {
        iec_init();
        return;
    }

    if (iec_bus.drive != NULL && iec_bus.drive->ready) {
        d64_drive_reset(iec_bus.drive);
    }

    iec_bus.listener_active = false;
    iec_bus.talker_active = false;
    iec_bus.listening = false;
    iec_bus.name_len = 0;

    iec_update_leds();
}

/*
 * Get IEC bus instance
 */
iec_bus_t *iec_get_bus(void)
{
    return &iec_bus;
}

/*
 * Listen to device
 */
static uint8_t iec_listen(int device)
{
    // We only support device 8
    if (device == 8) {
        if (iec_bus.drive != NULL && iec_bus.drive->ready) {
            iec_bus.listener = iec_bus.drive;
            iec_bus.listener_active = true;
            return ST_OK;
        }
    }

    iec_bus.listener_active = false;
    return ST_NOTPRESENT;
}

/*
 * Talk to device
 */
static uint8_t iec_talk(int device)
{
    // We only support device 8
    if (device == 8) {
        if (iec_bus.drive != NULL && iec_bus.drive->ready) {
            iec_bus.talker = iec_bus.drive;
            iec_bus.talker_active = true;
            return ST_OK;
        }
    }

    iec_bus.talker_active = false;
    return ST_NOTPRESENT;
}

/*
 * Unlisten - also handles pending OPEN command
 */
static uint8_t iec_unlisten(void)
{
    uint8_t st = ST_OK;

    // If we have a pending OPEN command with filename data, open the file now
    if (iec_bus.listener_active && iec_bus.received_cmd == CMD_OPEN && iec_bus.name_len > 0) {
        iec_bus.name_buf[iec_bus.name_len] = 0;  // Null-terminate
        if (iec_bus.listener != NULL) {
            iec_bus.listener->led = DRVLED_ON;
            iec_update_leds();
            st = d64_drive_open(iec_bus.listener, iec_bus.sec_addr,
                               iec_bus.name_buf, iec_bus.name_len);
        }
        iec_bus.name_len = 0;  // Clear for next command
    }

    iec_bus.listener_active = false;
    return st;
}

/*
 * Untalk
 */
static uint8_t iec_untalk(void)
{
    iec_bus.talker_active = false;
    return ST_OK;
}

/*
 * Secondary address after Listen
 */
static uint8_t iec_sec_listen(void)
{
    switch (iec_bus.received_cmd) {
        case CMD_OPEN:
            // Prepare for receiving the file name
            iec_bus.name_ptr = iec_bus.name_buf;
            iec_bus.name_len = 0;
            return ST_OK;

        case CMD_CLOSE:
            // Close channel
            if (iec_bus.listener->led != DRVLED_ERROR_FLASH) {
                iec_bus.listener->led = DRVLED_OFF;
                iec_update_leds();
            }
            return d64_drive_close(iec_bus.listener, iec_bus.sec_addr);
    }
    return ST_OK;
}

/*
 * Secondary address after Talk
 */
static uint8_t iec_sec_talk(void)
{
    return ST_OK;
}

/*
 * Byte after Open command: store character in file name, open file on EOI
 */
static uint8_t iec_open_out(uint8_t byte, bool eoi)
{
    if (iec_bus.name_len < IEC_NAMEBUF_LENGTH) {
        *iec_bus.name_ptr++ = byte;
        iec_bus.name_len++;
    }

    if (eoi) {
        *iec_bus.name_ptr = 0;  // Null-terminate
        iec_bus.listener->led = DRVLED_ON;
        iec_update_leds();
        return d64_drive_open(iec_bus.listener, iec_bus.sec_addr,
                             iec_bus.name_buf, iec_bus.name_len);
    }

    return ST_OK;
}

/*
 * Write byte to channel
 */
static uint8_t iec_data_out(uint8_t byte, bool eoi)
{
    return d64_drive_write(iec_bus.listener, iec_bus.sec_addr, byte, eoi);
}

/*
 * Read byte from channel
 */
static uint8_t iec_data_in(uint8_t *byte)
{
    return d64_drive_read(iec_bus.talker, iec_bus.sec_addr, byte);
}

/*
 * Output byte to listener
 */
uint8_t iec_out(uint8_t byte, bool eoi)
{
    if (iec_bus.listener_active) {
        if (iec_bus.received_cmd == CMD_OPEN) {
            return iec_open_out(byte, eoi);
        }
        if (iec_bus.received_cmd == CMD_DATA) {
            return iec_data_out(byte, eoi);
        }
        return ST_TIMEOUT;
    }
    return ST_TIMEOUT;
}

/*
 * Output byte with ATN (Talk/Listen/Untalk/Unlisten)
 */
uint8_t iec_out_atn(uint8_t byte)
{
    uint8_t st;

    switch (byte & 0xf0) {
        case ATN_LISTEN:
            iec_bus.received_cmd = 0;
            iec_bus.sec_addr = 0;
            iec_bus.listening = true;
            return iec_listen(byte & 0x0f);

        case ATN_UNLISTEN:
            // Call unlisten BEFORE clearing state (it needs received_cmd)
            st = iec_unlisten();
            iec_bus.received_cmd = 0;
            iec_bus.sec_addr = 0;
            iec_bus.listening = false;
            return st;

        case ATN_TALK:
            iec_bus.received_cmd = 0;
            iec_bus.sec_addr = 0;
            iec_bus.listening = false;
            return iec_talk(byte & 0x0f);

        case ATN_UNTALK:
            iec_bus.received_cmd = 0;
            iec_bus.sec_addr = 0;
            iec_bus.listening = false;
            return iec_untalk();
    }
    return ST_TIMEOUT;
}

/*
 * Output secondary address
 */
uint8_t iec_out_sec(uint8_t byte)
{
    if (iec_bus.listening) {
        if (iec_bus.listener_active) {
            iec_bus.sec_addr = byte & 0x0f;
            iec_bus.received_cmd = byte & 0xf0;
            return iec_sec_listen();
        }
    } else {
        if (iec_bus.talker_active) {
            iec_bus.sec_addr = byte & 0x0f;
            // After TALK, secondary address is just channel number (0-15)
            // Command is implicitly DATA for reading
            iec_bus.received_cmd = CMD_DATA;
            return iec_sec_talk();
        }
    }
    return ST_TIMEOUT;
}

/*
 * Read byte from talker
 */
uint8_t iec_in(uint8_t *byte)
{
    if (iec_bus.talker_active && (iec_bus.received_cmd == CMD_DATA)) {
        return iec_data_in(byte);
    }

    *byte = 0;
    return ST_TIMEOUT;
}

/*
 * ATN control functions (not needed for emulation, kept for API compatibility)
 */
void iec_set_atn(void)
{
    // Only needed for real IEC
}

void iec_rel_atn(void)
{
    // Only needed for real IEC
}

void iec_turnaround(void)
{
    // Only needed for real IEC
}

void iec_release(void)
{
    // Only needed for real IEC
}

/*
 * Update drive LED display
 */
void iec_update_leds(void)
{
    // This can be used to update a visual LED indicator
    // For now, just a placeholder
}

/*
 * Mount disk image on the drive
 */
bool iec_mount_image(const char *path)
{
    if (!iec_initialized) {
        iec_init();
    }

    if (iec_bus.drive == NULL) {
        return false;
    }

    return d64_drive_mount(iec_bus.drive, path);
}

/*
 * Unmount current disk image
 */
void iec_unmount_image(void)
{
    if (iec_bus.drive != NULL) {
        d64_drive_unmount(iec_bus.drive);
    }
}

/*
 * Check if disk is mounted
 */
bool iec_is_mounted(void)
{
    if (iec_bus.drive == NULL) {
        return false;
    }
    return d64_drive_is_mounted(iec_bus.drive);
}

/*
 * Get drive LED state
 */
iec_led_t iec_get_led_state(void)
{
    if (iec_bus.drive == NULL) {
        return DRVLED_OFF;
    }
    return iec_bus.drive->led;
}

/*
 * Get current error message
 */
const char *iec_get_error_string(void)
{
    if (iec_bus.drive == NULL) {
        return "74,DRIVE NOT READY,00,00\r";
    }
    return iec_bus.drive->error_buf;
}

/*
 * Convert ASCII character to PETSCII
 */
uint8_t ascii_to_petscii(char c)
{
    if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))) {
        return c ^ 0x20;
    }
    return c;
}

/*
 * Convert ASCII string to PETSCII string
 */
void ascii_to_petscii_str(uint8_t *dest, const char *src, int n)
{
    while (n-- > 0 && (*dest++ = ascii_to_petscii(*src++))) {
        // Continue
    }
}

/*
 * Convert PETSCII character to ASCII
 */
char petscii_to_ascii(uint8_t c)
{
    if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))) {
        return c ^ 0x20;
    }
    if ((c >= 0xc1) && (c <= 0xda)) {
        return c ^ 0x80;
    }
    return c;
}

/*
 * Convert PETSCII string to ASCII string
 */
void petscii_to_ascii_str(char *dest, const uint8_t *src, int n)
{
    while (n-- > 0 && (*dest++ = petscii_to_ascii(*src++))) {
        // Continue
    }
}
