/*
 *  Prefs_rp2350.h - Simplified preferences for RP2350
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
 */

#ifndef PREFS_RP2350_H
#define PREFS_RP2350_H

#include <string>
#include <map>

// SID types
enum {
    SIDTYPE_NONE,           // SID emulation off
    SIDTYPE_DIGITAL_6581,   // Digital SID emulation (6581)
    SIDTYPE_DIGITAL_8580,   // Digital SID emulation (8580)
    SIDTYPE_SIDCARD         // SID card
};

// RAM expansion types
enum {
    REU_NONE,       // No REU
    REU_128K,       // 128K REU
    REU_256K,       // 256K REU
    REU_512K,       // 512K REU
    REU_GEORAM      // 512K GeoRAM
};

// Display types
enum {
    DISPTYPE_WINDOW,    // Window
    DISPTYPE_SCREEN     // Fullscreen
};

// Color palettes
enum {
    PALETTE_PEPTO,
    PALETTE_COLODORE
};

// Set of firmware ROM paths (simplified for RP2350)
struct ROMPaths {
    bool operator==(const ROMPaths& other) const {
        return BasicROMPath == other.BasicROMPath &&
               KernalROMPath == other.KernalROMPath &&
               CharROMPath == other.CharROMPath &&
               DriveROMPath == other.DriveROMPath;
    }
    bool operator!=(const ROMPaths& other) const {
        return !(*this == other);
    }

    std::string BasicROMPath;   // Path for BASIC ROM
    std::string KernalROMPath;  // Path for Kernal ROM
    std::string CharROMPath;    // Path for Char ROM
    std::string DriveROMPath;   // Path for Drive ROM
};

// Controller button mapping (map from button to C64 keycode)
using ButtonMapping = std::map<unsigned, unsigned>;

// Preferences data
class Prefs {
public:
    Prefs();

    void Check();
    ROMPaths SelectedROMPaths() const;
    ButtonMapping SelectedButtonMapping() const;

    int NormalCycles;           // Available CPU cycles in normal raster lines
    int BadLineCycles;          // Available CPU cycles in Bad Lines
    int CIACycles;              // CIA timer ticks per raster line
    int FloppyCycles;           // Available 1541 CPU cycles per line

    std::string DrivePath[4];   // Path for drive 8..11
    std::string TapePath;       // Path for drive 1

    int SIDType;                // SID emulation type
    int REUType;                // Type of RAM expansion
    int DisplayType;            // Display type (windowed or full-screen)
    int Palette;                // Color palette to use
    int Joystick1Port;          // Port that joystick 1 is connected to
    int Joystick2Port;          // Port that joystick 2 is connected to
    int ScalingNumerator;       // Window scaling numerator
    int ScalingDenominator;     // Window scaling denominator
    int TestMaxFrames;          // Maximum number of frames to run

    bool SpriteCollisions;      // Sprite collision detection is on
    bool JoystickSwap;          // Swap joysticks 1<->2
    bool TwinStick;             // Twin-stick control
    bool TapeRumble;            // Tape motor controller rumble
    bool LimitSpeed;            // Limit speed to 100%
    bool FastReset;             // Skip RAM test on reset
    bool CIAIRQHack;            // Write to CIA ICR clears IRQ
    bool MapSlash;              // Map '/' in C64 filenames
    bool Emul1541Proc;          // Enable processor-level 1541 emulation
    bool ShowLEDs;              // Show status bar
    bool AutoStart;             // Auto-start from drive 8 after reset
    bool TestBench;             // Enable features for automatic regression tests

    std::string LoadProgram;    // BASIC program file to load
    std::string ROMSet;         // Name of selected ROM set
    std::string ButtonMap;      // Name of selected controller button mapping
    std::string CartridgePath;  // Path for cartridge image file
    std::string TestScreenshotPath; // Path for test screenshot
};

// These are the active preferences
extern Prefs ThePrefs;

#endif // PREFS_RP2350_H
