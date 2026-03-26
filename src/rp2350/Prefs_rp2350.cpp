/*
 *  Prefs_rp2350.cpp - Simplified preferences for RP2350
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Minimal preferences without filesystem or complex STL.
 *  All settings are hardcoded for embedded use.
 */

#include "Prefs_rp2350.h"

// Global preferences instance
Prefs ThePrefs;


/*
 *  Constructor - Initialize with defaults for RP2350
 */

Prefs::Prefs()
{
    // CPU cycle timing (PAL defaults)
    NormalCycles = 63;
    BadLineCycles = 23;
    CIACycles = 63;
    FloppyCycles = 64;

    // Drive paths - empty on RP2350, set via disk loader
    for (int i = 0; i < 4; i++) {
        DrivePath[i] = "";
    }
    TapePath = "";

    // SID type - digital 6581 emulation
    SIDType = SIDTYPE_DIGITAL_6581;

    // No RAM expansion by default (save memory)
    REUType = REU_NONE;

    // Display settings (not used on RP2350, but keep defaults)
    DisplayType = DISPTYPE_WINDOW;
    Palette = PALETTE_PEPTO;
    ScalingNumerator = 2;
    ScalingDenominator = 1;

    // Joystick ports - use NES gamepad
    Joystick1Port = 0;
    Joystick2Port = 1;

    // Sprite collisions on
    SpriteCollisions = true;

    // Joystick settings
    JoystickSwap = false;
    TwinStick = false;
    TapeRumble = false;

    // Speed limiting on (important for proper emulation)
    LimitSpeed = true;

    // Fast reset on (skip RAM test)
    FastReset = true;

    // CIA IRQ hack off
    CIAIRQHack = false;

    // Map slash in filenames
    MapSlash = true;

    // Use IEC emulation, not processor-level 1541
    // (processor-level is too CPU intensive for RP2350)
    Emul1541Proc = false;

    // Show LEDs (drive activity)
    ShowLEDs = true;

    // No auto-start
    AutoStart = false;

    // No test bench mode
    TestBench = false;
    TestMaxFrames = 0;

    // ROM set - use built-in
    ROMSet = "";

    // Button mapping - empty (use defaults)
    ButtonMap = "";

    // No cartridge
    CartridgePath = "";

    // No program to load
    LoadProgram = "";

    // No screenshot path
    TestScreenshotPath = "";
}


/*
 *  Check - Validate preferences
 */

void Prefs::Check()
{
    // Ensure sane values
    if (NormalCycles < 1) NormalCycles = 63;
    if (BadLineCycles < 1) BadLineCycles = 23;
    if (CIACycles < 1) CIACycles = 63;
    if (FloppyCycles < 1) FloppyCycles = 64;
}


/*
 *  SelectedROMPaths - Return empty paths (use built-in ROMs)
 */

ROMPaths Prefs::SelectedROMPaths() const
{
    ROMPaths paths;
    // All empty - will use built-in ROMs
    return paths;
}


/*
 *  SelectedButtonMapping - Return empty mapping
 */

ButtonMapping Prefs::SelectedButtonMapping() const
{
    ButtonMapping mapping;
    return mapping;
}
