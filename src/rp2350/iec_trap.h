/*
 *  iec_trap.h - IEC bus KERNAL trap handler
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

#ifndef IEC_TRAP_H
#define IEC_TRAP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * KERNAL IEC routine addresses (internal implementations)
 * These are the actual routine addresses, not the jump table vectors
 */
#define KERNAL_TALK_IMPL    0xED09
#define KERNAL_LISTEN_IMPL  0xED0C
#define KERNAL_SECOND_IMPL  0xEDB9
#define KERNAL_TKSA_IMPL    0xEDC7
#define KERNAL_CIOUT_IMPL   0xEDDD
#define KERNAL_UNTLK_IMPL   0xEDEF
#define KERNAL_UNLSN_IMPL   0xEDFE
#define KERNAL_ACPTR_IMPL   0xEE13

/*
 * C64 RAM locations used by IEC routines
 */
#define C64_STATUS      0x90    // Serial bus status byte (ST)
#define C64_DESSION     0x97    // Device listening (bit 7) or talking (bit 6)
#define C64_FA          0xBA    // Current device number
#define C64_SA          0xB9    // Current secondary address
#define C64_SENESSION   0xA3    // EOI flag (bit 7)

/*
 * Initialize IEC trap handler
 * Must be called before using any other functions
 */
void iec_trap_init(void);

/*
 * Check if address is a trapped IEC routine
 * Returns true if the address should be trapped
 */
bool iec_trap_check(uint16_t pc);

/*
 * Process IEC trap at given address
 * Updates CPU registers and RAM as needed
 * Returns new PC value (RTS target)
 *
 * Parameters:
 *   pc - Current program counter (trap address)
 *   a, x, y - CPU registers (may be modified)
 *   sp - Stack pointer (modified for RTS)
 *   status - CPU status register (carry flag may be modified)
 *   ram - Pointer to C64 RAM
 */
uint16_t iec_trap_process(uint16_t pc, uint8_t *a, uint8_t *x, uint8_t *y,
                         uint8_t *sp, uint8_t *status, uint8_t *ram);

/*
 * Check if IEC traps are enabled
 */
bool iec_trap_enabled(void);

/*
 * Enable/disable IEC traps
 */
void iec_trap_set_enabled(bool enabled);

/*
 * Mount disk image for IEC trap handler
 * Returns true on success
 */
bool iec_trap_mount(const char *path);

/*
 * Unmount current disk image
 */
void iec_trap_unmount(void);

/*
 * Check if disk is mounted
 */
bool iec_trap_is_mounted(void);

#ifdef __cplusplus
}
#endif

#endif // IEC_TRAP_H
