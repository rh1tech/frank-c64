/*
 *  debug_log.h - Debug logging macros for RP2350
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#pragma once

#include <stdio.h>

#ifndef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0
#endif

#if ENABLE_DEBUG_LOGS
#define MII_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define MII_DEBUG_PRINTF(...) do { } while (0)
#endif
