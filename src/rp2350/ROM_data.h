/*
 *  ROM_data.h - Standalone ROM data for RP2350
 *
 *  MurmC64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  C64/1541 ROMs (C) Commodore Business Machines
 */

#ifndef ROM_DATA_H
#define ROM_DATA_H

#include <cstdint>
#include "../board_config.h"

// ROM declarations (defined in ROM_data.cpp)
// Uses sizes from board_config.h
extern const uint8_t BuiltinBasicROM[BASIC_ROM_SIZE];
extern const uint8_t BuiltinCharROM[CHAR_ROM_SIZE];

#include "c64_1541.rom.h"
#include "c64_fast_reset.rom.h"

#endif // ROM_DATA_H
