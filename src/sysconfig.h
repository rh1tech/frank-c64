/*
 *  sysconfig.h - System configuration for FRANK C64
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This file defines platform-specific configuration.
 */

#ifndef SYSCONFIG_H
#define SYSCONFIG_H

//=============================================================================
// Platform Detection
//=============================================================================

#ifdef FRODO_RP2350
// RP2350 platform
#define FRODO_PLATFORM "RP2350"
#else
// Desktop platform (SDL)
#define FRODO_PLATFORM "Desktop"
#endif

//=============================================================================
// Feature Configuration
//=============================================================================

// Disable features not supported on RP2350
#ifdef FRODO_RP2350
// No GTK GUI
#undef HAVE_GTK
// No filesystem-based prefs
#define PREFS_MINIMAL 1
// No rewind buffer (too much memory)
#define NO_REWIND_BUFFER 1
// No cartridge ROM auto-loading
#define NO_AUTO_CARTRIDGE 1
#endif

//=============================================================================
// NTSC/PAL Configuration
//=============================================================================

// Default to PAL timing
#ifndef NTSC
// PAL mode (default)
#endif

//=============================================================================
// Optimization Hints
//=============================================================================

// Allow compiler to assume unaligned access is OK
// (RP2350 Cortex-M33 handles unaligned access)
#ifdef FRODO_RP2350
#define CAN_ACCESS_UNALIGNED
#endif

#endif // SYSCONFIG_H
