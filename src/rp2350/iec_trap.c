/*
 *  iec_trap.c - IEC bus KERNAL trap handler
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This module intercepts KERNAL IEC bus calls to provide
 *  DOS-level disk emulation without full serial bus timing.
 *  Based on Frodo4 by Christian Bauer.
 */

#include <stdio.h>
#include <string.h>
#include "iec_trap.h"
#include "iec.h"
#include "debug_log.h"

/*
 * Trap state
 */
static bool traps_enabled = true;
static bool trap_initialized = false;

/*
 * Initialize IEC trap handler
 */
void iec_trap_init(void)
{
    if (trap_initialized) {
        return;
    }

    iec_init();
    traps_enabled = true;
    trap_initialized = true;

    MII_DEBUG_PRINTF("IEC trap: initialized (enabled=%d, init=%d)\n", traps_enabled, trap_initialized);
}

/*
 * Check if IEC traps are enabled
 */
bool iec_trap_enabled(void)
{
    return traps_enabled && trap_initialized;
}

/*
 * Enable/disable IEC traps
 */
void iec_trap_set_enabled(bool enabled)
{
    traps_enabled = enabled;
}

/*
 * Pop address from stack (for RTS simulation)
 */
static uint16_t pop_address(uint8_t *sp, uint8_t *ram)
{
    (*sp)++;
    uint16_t lo = ram[0x100 + *sp];
    (*sp)++;
    uint16_t hi = ram[0x100 + *sp];
    return (hi << 8) | lo;
}

/*
 * Handle LISTEN trap ($ED0C)
 * A = Device number
 */
static uint16_t trap_listen(uint8_t *a, uint8_t *sp, uint8_t *ram)
{
    uint8_t device = *a;

    // Store device number
    ram[C64_FA] = device;

    // Send LISTEN command
    uint8_t st = iec_out_atn(0x20 | (device & 0x1f));

    // Update status
    ram[C64_STATUS] |= st;

    // Set listening flag
    ram[C64_DESSION] |= 0x80;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle TALK trap ($ED09)
 * A = Device number
 */
static uint16_t trap_talk(uint8_t *a, uint8_t *sp, uint8_t *ram)
{
    uint8_t device = *a;

    // Store device number
    ram[C64_FA] = device;

    // Send TALK command
    uint8_t st = iec_out_atn(0x40 | (device & 0x1f));

    // Update status
    ram[C64_STATUS] |= st;

    // Set talking flag
    ram[C64_DESSION] |= 0x40;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle SECOND trap ($EDB9)
 * A = Secondary address byte (includes command: $F0=OPEN, $E0=CLOSE, $60=DATA)
 */
static uint16_t trap_second(uint8_t *a, uint8_t *sp, uint8_t *ram)
{
    uint8_t sa = *a;

    // Store secondary address (channel number only)
    ram[C64_SA] = sa & 0x0f;

    // Send secondary address byte as-is (preserves command bits)
    uint8_t st = iec_out_sec(sa);

    // Update status
    ram[C64_STATUS] |= st;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle TKSA trap ($EDC7)
 * A = Secondary address byte (includes command bits)
 */
static uint16_t trap_tksa(uint8_t *a, uint8_t *sp, uint8_t *ram)
{
    uint8_t sa = *a;

    // Store secondary address (channel number only)
    ram[C64_SA] = sa & 0x0f;

    // Send secondary address byte as-is (preserves command bits)
    uint8_t st = iec_out_sec(sa);

    // Turnaround
    iec_turnaround();

    // Update status
    ram[C64_STATUS] |= st;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle CIOUT trap ($EDDD)
 * A = Byte to send
 */
static uint16_t trap_ciout(uint8_t *a, uint8_t *sp, uint8_t *ram)
{
    uint8_t byte = *a;

    // Check if this is EOI (last byte)
    // The KERNAL sets bit 7 of $A3 (SENESSION) for EOI
    bool eoi = (ram[C64_SENESSION] & 0x80) != 0;

    // Send byte
    uint8_t st = iec_out(byte, eoi);

    // Update status
    ram[C64_STATUS] |= st;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle UNTLK trap ($EDEF)
 */
static uint16_t trap_untlk(uint8_t *sp, uint8_t *ram)
{
    // Send UNTALK
    iec_set_atn();
    iec_out_atn(0x5f);
    iec_rel_atn();
    iec_release();

    // Clear talking flag
    ram[C64_DESSION] &= ~0x40;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle UNLSN trap ($EDFE)
 */
static uint16_t trap_unlsn(uint8_t *sp, uint8_t *ram)
{
    // Send UNLISTEN
    iec_out_atn(0x3f);
    iec_release();

    // Clear listening flag
    ram[C64_DESSION] &= ~0x80;

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Handle ACPTR trap ($EE13)
 * Returns byte in A
 */
static uint16_t trap_acptr(uint8_t *a, uint8_t *sp, uint8_t *status, uint8_t *ram)
{
    uint8_t byte;

    // Receive byte
    uint8_t st = iec_in(&byte);

    // Return byte in A
    *a = byte;

    // Update status
    ram[C64_STATUS] |= st;

    // Clear carry on success, set on error
    if (st & (ST_TIMEOUT | ST_NOTPRESENT)) {
        *status |= 0x01;  // Set carry
    } else {
        *status &= ~0x01; // Clear carry
    }

    // Return from KERNAL routine
    return pop_address(sp, ram) + 1;
}

/*
 * Check if address is a trapped IEC routine
 */
bool iec_trap_check(uint16_t pc)
{
    if (!traps_enabled || !trap_initialized) {
        return false;
    }

    // Debug: log when PC is in KERNAL IEC area
    static int iec_area_count = 0;
    if (pc >= 0xED00 && pc <= 0xEE20) {
        if (iec_area_count < 20) {
            MII_DEBUG_PRINTF("IEC trap check: PC=$%04X\n", pc);
            iec_area_count++;
        }
    }

    switch (pc) {
        case KERNAL_TALK_IMPL:
        case KERNAL_LISTEN_IMPL:
        case KERNAL_SECOND_IMPL:
        case KERNAL_TKSA_IMPL:
        case KERNAL_CIOUT_IMPL:
        case KERNAL_UNTLK_IMPL:
        case KERNAL_UNLSN_IMPL:
        case KERNAL_ACPTR_IMPL:
            MII_DEBUG_PRINTF("IEC TRAP HIT: PC=$%04X\n", pc);
            return true;
        default:
            return false;
    }
}

/*
 * Process IEC trap at given address
 */
uint16_t iec_trap_process(uint16_t pc, uint8_t *a, uint8_t *x, uint8_t *y,
                         uint8_t *sp, uint8_t *status, uint8_t *ram)
{
    (void)x;
    (void)y;

    switch (pc) {
        case KERNAL_TALK_IMPL:
            return trap_talk(a, sp, ram);

        case KERNAL_LISTEN_IMPL:
            return trap_listen(a, sp, ram);

        case KERNAL_SECOND_IMPL:
            return trap_second(a, sp, ram);

        case KERNAL_TKSA_IMPL:
            return trap_tksa(a, sp, ram);

        case KERNAL_CIOUT_IMPL:
            return trap_ciout(a, sp, ram);

        case KERNAL_UNTLK_IMPL:
            return trap_untlk(sp, ram);

        case KERNAL_UNLSN_IMPL:
            return trap_unlsn(sp, ram);

        case KERNAL_ACPTR_IMPL:
            return trap_acptr(a, sp, status, ram);

        default:
            // Should not happen
            return pc;
    }
}

/*
 * Mount disk image for IEC trap handler
 */
bool iec_trap_mount(const char *path)
{
    if (!trap_initialized) {
        iec_trap_init();
    }
    return iec_mount_image(path);
}

/*
 * Unmount current disk image
 */
void iec_trap_unmount(void)
{
    iec_unmount_image();
}

/*
 * Check if disk is mounted
 */
bool iec_trap_is_mounted(void)
{
    return iec_is_mounted();
}
