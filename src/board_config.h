/*
 * board_config.h
 *
 * Board configuration for MurmC64 - Commodore 64 emulator (Frodo4) for RP2350
 * Based on murmapple board configuration
 *
 * Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"

/*
 * Board Configuration Variants:
 *
 * BOARD_M1 - M1 GPIO layout
 * BOARD_M2 - M2 GPIO layout
 *
 * PSRAM pin is auto-detected based on chip package:
 *   RP2350B: GPIO47 (for both M1 and M2)
 *   RP2350A: GPIO19 (M1) or GPIO8 (M2)
 *
 * M1 GPIO Layout:
 *   HDMI: CLKN=6, CLKP=7, D0N=8, D0P=9, D1N=10, D1P=11, D2N=12, D2P=13
 *   SD:   CLK=2, CMD=3, DAT0=4, DAT3=5
 *   PS/2: CLK=0, DATA=1
 *
 * M2 GPIO Layout:
 *   HDMI: CLKN=12, CLKP=13, D0N=14, D0P=15, D1N=16, D1P=17, D2N=18, D2P=19
 *   SD:   CLK=6, CMD=7, DAT0=4, DAT3=5
 *   PS/2: CLK=2, DATA=3
 */

// Default to M1 if no config specified
#if !defined(BOARD_M1) && !defined(BOARD_M2) && !defined(BOARD_PC) && !defined(BOARD_Z0)
#define BOARD_M1
#endif

//=============================================================================
// CPU/PSRAM Speed Defaults (can be overridden via CMake)
//=============================================================================
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_50
#endif

#if PICO_RP2350
//=============================================================================
// PSRAM Configuration
//=============================================================================

// PSRAM pin for RP2350A variants
#ifdef BOARD_M1
#define PSRAM_PIN_RP2350A 19
#else
#define PSRAM_PIN_RP2350A 8
#endif

// PSRAM pin for RP2350B (always GPIO47)
#define PSRAM_PIN_RP2350B 47

// Runtime function to get PSRAM pin based on chip package
static inline uint get_psram_pin(void) {
    uint32_t package_sel = *((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    if (package_sel & 1) {
        return PSRAM_PIN_RP2350A;
    } else {
        return PSRAM_PIN_RP2350B;
    }
}
#endif

//=============================================================================
// M1 Layout Configuration
//=============================================================================
#ifdef BOARD_M1

// HDMI Pins
#define HDMI_PIN_CLKN 6
#define HDMI_PIN_CLKP 7
#define HDMI_PIN_D0N  8
#define HDMI_PIN_D0P  9
#define HDMI_PIN_D1N  10
#define HDMI_PIN_D1P  11
#define HDMI_PIN_D2N  12
#define HDMI_PIN_D2P  13

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    2
#define SDCARD_PIN_CMD    3
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// NES/SNES Gamepad Pins (directly after HDMI pins)
#define NESPAD_GPIO_CLK   14
#define NESPAD_GPIO_DATA  16
#define NESPAD_GPIO_LATCH 15

// I2S Audio Pins
#ifndef I2S_DATA_PIN
#define I2S_DATA_PIN       26
#define I2S_CLOCK_PIN_BASE 27
#endif

#define PWM_RIGHT_PIN 26
#define PWM_LEFT_PIN 27
#define BEEPER_PIN 28

#endif // BOARD_M1

//=============================================================================
// M2 Layout Configuration
//=============================================================================
#ifdef BOARD_M2

// HDMI Pins
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    6
#define SDCARD_PIN_CMD    7
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// NES/SNES Gamepad Pins (using available GPIOs)
#define NESPAD_GPIO_CLK   20
#define NESPAD_GPIO_DATA  22
#define NESPAD_GPIO_LATCH 21

// I2S Audio Pins
#define I2S_DATA_PIN       9
#define I2S_CLOCK_PIN_BASE 10

#define BEEPER_PIN 9
#define PWM_RIGHT_PIN 10
#define PWM_LEFT_PIN 11

#endif // BOARD_M2

//=============================================================================
// Olimex PICO-PC Layout Configuration
//=============================================================================
#ifdef BOARD_PC

// HDMI Pins
#define HDMI_PIN_CLKN 12

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    6
#define SDCARD_PIN_CMD    7
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     22

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// NES/SNES Gamepad Pins (directly after HDMI pins)
#define NESPAD_GPIO_CLK   5
#define NESPAD_GPIO_DATA  20
#define NESPAD_GPIO_LATCH 9

#define PWM_RIGHT_PIN 27
#define PWM_LEFT_PIN 28

#endif // BOARD_PC

//=============================================================================
// Wavshare RP2350-PiZero Layout Configuration
//=============================================================================
#ifdef BOARD_Z0

// HDMI Pins
#define HDMI_PIN_CLKN 32

#define HDMI_BASE_PIN HDMI_PIN_CLKN

#define HDMI_PIN_RGB_notBGR (0)
#define HDMI_PIN_invert_diffpairs (0)
#define beginHDMI_PIN_data (HDMI_BASE_PIN)
#define beginHDMI_PIN_clk (HDMI_BASE_PIN + 6)

// SD Card Pins
#define SDCARD_SPI_BUS    spi1
#define SDCARD_PIN_CLK    30
#define SDCARD_PIN_CMD    31
#define SDCARD_PIN_D0     40
#define SDCARD_PIN_D3     43

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// NES/SNES Gamepad Pins (directly after HDMI pins)
#define NESPAD_GPIO_CLK   4
#define NESPAD_GPIO_LATCH 5
#define NESPAD_GPIO_DATA  7

#define PWM_RIGHT_PIN 10
#define PWM_LEFT_PIN 11

#endif // BOARD_Z0

//=============================================================================
// Commodore 64 Display Configuration
//=============================================================================

// C64 VIC-II display dimensions (from Frodo4)
// DISPLAY_X = 0x180 = 384 pixels
// DISPLAY_Y = 0x110 = 272 lines
#define C64_DISPLAY_WIDTH   384
#define C64_DISPLAY_HEIGHT  272

// HDMI output resolution (we scale C64 to fit 320x240)
// The C64 display will be centered with slight cropping
#define HDMI_WIDTH  640
#define HDMI_HEIGHT 480

// Framebuffer configuration
// We use 320x240 output, scaling C64's 384x272 with some border cropping
// Visible area: ~320 pixels wide (from 384), ~240 lines (from 272)
#define FB_WIDTH   320
#define FB_HEIGHT  240

// Border cropping offsets (to center the visible C64 screen)
// Left border: (384-320)/2 = 32 pixels
// Top border: (272-240)/2 = 16 lines
#define C64_CROP_LEFT   32
#define C64_CROP_TOP    16

//=============================================================================
// C64 Memory Configuration
//=============================================================================

// C64 memory sizes (from Frodo4)
#define C64_RAM_SIZE        0x10000     // 64 KB main RAM
#define COLOR_RAM_SIZE      0x400       // 1 KB color RAM
#define BASIC_ROM_SIZE      0x2000      // 8 KB BASIC ROM
#define KERNAL_ROM_SIZE     0x2000      // 8 KB Kernal ROM
#define CHAR_ROM_SIZE       0x1000      // 4 KB Character ROM
#define DRIVE_RAM_SIZE      0x800       // 2 KB 1541 drive RAM
#define DRIVE_ROM_SIZE      0x4000      // 16 KB 1541 drive ROM

//=============================================================================
// Memory Placement Strategy
//=============================================================================
//
// SRAM (520KB total, fast access):
//   - Color RAM (1KB) - needs fast VIC access
//   - 1541 Drive RAM (2KB) - needs fast CPU access
//   - Audio buffers (~8KB)
//   - Framebuffer for HDMI DMA (320*240 = 77KB)
//   - Stack, heap, code data
//
// PSRAM (8MB, via QSPI):
//   - C64 main RAM (64KB)
//   - Disk images and file buffers
//   - Snapshot/save state storage (if implemented)
//
// Flash (XIP - execute in place):
//   - BASIC ROM (8KB)
//   - Kernal ROM (8KB)
//   - Character ROM (4KB)
//   - 1541 Drive ROM (16KB)
//   - SID wave tables (~16KB)
//   - Program code
//

//=============================================================================
// C64 Timing Constants
//=============================================================================

#ifdef NTSC
// NTSC timing
#define C64_SCREEN_FREQ     60
#define C64_CYCLES_PER_LINE 65
#define C64_TOTAL_RASTERS   263
#define C64_CPU_FREQ        1022727     // ~1.02 MHz
#else
// PAL timing (default)
#define C64_SCREEN_FREQ     50
#define C64_CYCLES_PER_LINE 63
#define C64_TOTAL_RASTERS   312
#define C64_CPU_FREQ        985248      // ~0.985 MHz
#endif

// Cycles per frame
#define C64_CYCLES_PER_FRAME (C64_CYCLES_PER_LINE * C64_TOTAL_RASTERS)

//=============================================================================
// Audio Configuration
//=============================================================================

// SID audio sample rate
#define SID_SAMPLE_RATE     44100

// Audio buffer size in samples (per channel)
// At 44100 Hz, 512 samples = ~12ms latency (increased for stability)
// Smaller buffers = less latency but more CPU pressure
#define SID_BUFFER_SAMPLES  512

// Number of audio buffers for triple buffering (helps prevent underruns)
#define SID_BUFFER_COUNT    4

#endif // BOARD_CONFIG_H
