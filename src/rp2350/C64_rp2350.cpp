/*
 *  C64_rp2350.cpp - C64 emulator for RP2350
 *
 *  MurmC64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This implements the full C64 emulation using the Frodo4 core chips:
 *  - MOS6510 (CPU)
 *  - MOS6569 (VIC-II)
 *  - MOS6581 (SID)
 *  - MOS6526 (CIA1 & CIA2)
 *  - MOS6502_1541 (1541 drive CPU)
 *  - GCRDisk (1541 disk emulation)
 *  - IEC (serial bus)
 *
 *  Core 0 runs all emulation.
 *  Audio samples are sent to I2S via ring buffer (sid_i2s.cpp).
 */

#include "../sysdeps.h"
#include "../C64.h"
#include "../CPUC64.h"
#include "../VIC.h"
#include "../SID.h"
#include "../CIA.h"
#include "../IEC.h"
#include "../Cartridge.h"
#include "../1541gcr.h"
#include "../CPU1541.h"
#include "../Prefs.h"

// RP2350-specific headers
extern "C" {
#include "debug_log.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "fatfs/ff.h"
}

// Platform-specific
#include "Display_rp2350.h"
#include "ROM_data.h"

#include <cstring>
#include <cstdlib>

// Global flag for Frodo SC mode
bool IsFrodoSC = false;

// Global C64 instance (simplified for RP2350)
C64 *TheC64 = nullptr;

// External display pointer
static Display *g_display = nullptr;
static uint8_t g_RAM[C64_RAM_SIZE] __aligned(4);
static uint8_t g_RAM1541[DRIVE_RAM_SIZE] __aligned(4);
static uint8_t g_Color[COLOR_RAM_SIZE] __aligned(4);

/*
 *  C64 Constructor (simplified for RP2350)
 */

C64::C64() : quit_requested(false), prefs_editor_requested(false), load_snapshot_requested(false)
{
    MII_DEBUG_PRINTF("C64: Allocating memory...\n");

    RAM = g_RAM;
    RAM1541 = g_RAM1541;
    Basic = BuiltinBasicROM;
    Kernal = c64_fast_reset_rom;
    Char = BuiltinCharROM;
    ROM1541 = c64_1541_rom;
    // Color RAM in regular SRAM for fast VIC access
    Color = g_Color;

    MII_DEBUG_PRINTF("C64: Memory allocated OK\n");

    // Open display
    MII_DEBUG_PRINTF("C64: Creating Display...\n");
    TheDisplay = new Display(this);
    g_display = TheDisplay;
    MII_DEBUG_PRINTF("C64: Display created\n");

    // Initialize memory with powerup pattern
    MII_DEBUG_PRINTF("C64: Initializing memory...\n");
    init_memory();
    MII_DEBUG_PRINTF("C64: Memory initialized\n");

    MII_DEBUG_PRINTF("C64: Creating chips...\n");
    // Create the chips
    MII_DEBUG_PRINTF("  Creating CPU...\n");
    TheCPU = new MOS6510(this, RAM, Basic, Kernal, Char, Color);
    MII_DEBUG_PRINTF("  CPU created\n");

    MII_DEBUG_PRINTF("  Creating 1541...\n");
    TheGCRDisk = new GCRDisk(RAM1541);
    TheCPU1541 = new MOS6502_1541(this, TheGCRDisk, RAM1541, ROM1541);
    TheGCRDisk->SetCPU(TheCPU1541);
    MII_DEBUG_PRINTF("  1541 created\n");

    MII_DEBUG_PRINTF("  Creating VIC...\n");
    TheVIC = new MOS6569(this, TheDisplay, TheCPU, RAM, Char, Color);
    MII_DEBUG_PRINTF("  VIC created\n");

    MII_DEBUG_PRINTF("  Creating SID...\n");
    TheSID = new MOS6581;
    MII_DEBUG_PRINTF("  SID created\n");

    MII_DEBUG_PRINTF("  Creating CIAs...\n");
    TheCIA1 = new MOS6526_1(TheCPU, TheVIC);
    TheCIA2 = TheCPU1541->TheCIA2 = new MOS6526_2(TheCPU, TheVIC, TheCPU1541);
    MII_DEBUG_PRINTF("  CIAs created\n");

    MII_DEBUG_PRINTF("  Creating IEC...\n");
    TheIEC = new IEC(this);
    MII_DEBUG_PRINTF("  IEC created\n");

    // No tape support on RP2350
    TheTape = nullptr;

    // No cartridge by default
    TheCart = new NoCartridge;

    TheCPU->SetChips(TheVIC, TheSID, TheCIA1, TheCIA2, TheCart, TheIEC, TheTape);

    joykey = 0xff;
    cycle_counter = 0;

    MII_DEBUG_PRINTF("C64: Initialization complete\n");
}


/*
 *  C64 Destructor
 */

C64::~C64()
{
    delete TheTape;
    delete TheGCRDisk;
    delete TheCart;
    delete TheIEC;
    delete TheCIA2;
    delete TheCIA1;
    delete TheSID;
    delete TheVIC;
    delete TheCPU1541;
    delete TheCPU;
    delete TheDisplay;
}


/*
 *  Initialize emulation memory with powerup pattern
 */

void C64::init_memory()
{
    // Simplified powerup pattern (zeros with some standard values)
    memset(RAM, 0, C64_RAM_SIZE);

    // Set some standard power-on values
    RAM[0x0000] = 0x2f;  // Data direction register
    RAM[0x0001] = 0x37;  // CPU port

    // Initialize color RAM with random values
    for (unsigned i = 0; i < COLOR_RAM_SIZE; ++i) {
        Color[i] = rand() & 0x0f;
    }

    // Clear 1541 RAM
    memset(RAM1541, 0, DRIVE_RAM_SIZE);
}


/*
 *  Apply ROM patch
 *

static void apply_patch(bool apply, uint8_t *rom, const uint8_t *builtin,
                        uint16_t offset, unsigned size, const uint8_t *patch)
{
    if (apply) {
        if (memcmp(rom + offset, builtin + offset, size) == 0) {
            memcpy(rom + offset, patch, size);
        }
    } else {
        if (memcmp(rom + offset, patch, size) == 0) {
            memcpy(rom + offset, builtin + offset, size);
        }
    }
}


/*
 *  Patch ROMs for fast reset and IEC emulation
 */

void C64::patch_roms(bool fast_reset, bool emul_1541_proc)
{
    /*
    // Fast reset patch - skip RAM test
    static const uint8_t fast_reset_patch[] = { 0xa0, 0x00 };
    apply_patch(fast_reset, Kernal, BuiltinKernalROM, 0x1d84,
                sizeof(fast_reset_patch), fast_reset_patch);

    // IEC patches for non-processor-level disk emulation
    static const uint8_t iec_patch_1[] = { 0xf2, 0x00 };  // IECOut
    static const uint8_t iec_patch_2[] = { 0xf2, 0x01 };  // IECOutATN
    static const uint8_t iec_patch_3[] = { 0xf2, 0x02 };  // IECOutSec
    static const uint8_t iec_patch_4[] = { 0xf2, 0x03 };  // IECIn
    static const uint8_t iec_patch_5[] = { 0xf2, 0x04 };  // IECSetATN
    static const uint8_t iec_patch_6[] = { 0xf2, 0x05 };  // IECRelATN
    static const uint8_t iec_patch_7[] = { 0xf2, 0x06 };  // IECTurnaround
    static const uint8_t iec_patch_8[] = { 0xf2, 0x07 };  // IECRelease

    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0d40,
                sizeof(iec_patch_1), iec_patch_1);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0d23,
                sizeof(iec_patch_2), iec_patch_2);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0d36,
                sizeof(iec_patch_3), iec_patch_3);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0e13,
                sizeof(iec_patch_4), iec_patch_4);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0def,
                sizeof(iec_patch_5), iec_patch_5);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0dbe,
                sizeof(iec_patch_6), iec_patch_6);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0dcc,
                sizeof(iec_patch_7), iec_patch_7);
    apply_patch(!emul_1541_proc, Kernal, BuiltinKernalROM, 0x0e03,
                sizeof(iec_patch_8), iec_patch_8);

    // 1541 patches - skip ROM checksum
    static const uint8_t drive_patch_1[] = { 0xea, 0xea };
    static const uint8_t drive_patch_2[] = { 0xf2, 0x00 };

    apply_patch(true, ROM1541, BuiltinDriveROM, 0x2ae4,
                sizeof(drive_patch_1), drive_patch_1);
    apply_patch(true, ROM1541, BuiltinDriveROM, 0x2ae8,
                sizeof(drive_patch_1), drive_patch_1);
    apply_patch(true, ROM1541, BuiltinDriveROM, 0x2c9b,
                sizeof(drive_patch_2), drive_patch_2);
    FIL f;
    f_open(&f, "/c64_fast_reset.rom", FA_CREATE_ALWAYS | FA_WRITE);
    UINT bw;
    f_write(&f, Kernal, KERNAL_ROM_SIZE, &bw);
    f_close(&f);
*/
}


/*
 *  Reset C64
 */

void C64::Reset(bool clear_memory)
{
    TheCPU->AsyncReset();
    TheCPU1541->AsyncReset();
    TheGCRDisk->Reset();
    TheSID->Reset();
    TheCIA1->Reset();
    TheCIA2->Reset();
    TheIEC->Reset();
    TheCart->Reset();

    if (clear_memory) {
        init_memory();
    }

    play_mode = PlayMode::Play;
}


/*
 *  Reset C64 and auto-start from drive 8
 */

void C64::ResetAndAutoStart()
{
//    patch_roms(ThePrefs.FastReset, ThePrefs.Emul1541Proc);
    Reset(true);
}


/*
 *  NMI C64
 */

void C64::NMI()
{
    TheCPU->AsyncNMI();
}


/*
 *  Preferences have changed
 */

void C64::NewPrefs(const Prefs *prefs)
{
    MII_DEBUG_PRINTF("NewPrefs: Emul1541Proc changing from %d to %d\n",
           ThePrefs.Emul1541Proc, prefs->Emul1541Proc);

    TheDisplay->NewPrefs(prefs);
    TheIEC->NewPrefs(prefs);
    TheGCRDisk->NewPrefs(prefs);
    TheSID->NewPrefs(prefs);

    if (ThePrefs.Emul1541Proc != prefs->Emul1541Proc) {
        MII_DEBUG_PRINTF("NewPrefs: Resetting 1541 CPU\n");
        TheCPU1541->AsyncReset();
    }
}


/*
 *  Request emulator quit
 */

void C64::RequestQuit(int exit_code)
{
    main_loop_exit_code = exit_code;
    quit_requested = true;
}


void C64::RequestPrefsEditor()
{
    prefs_editor_requested = true;
}


void C64::RequestLoadSnapshot(const std::string &path)
{
    requested_snapshot = path;
    load_snapshot_requested = true;
}


/*
 *  Mount disk drive (simplified for RP2350)
 */

void C64::MountDrive8(bool emul_1541_proc, const char *path)
{
    MII_DEBUG_PRINTF("MountDrive8: path=%s, emul_1541=%d\n", path, emul_1541_proc);

    // Update preferences
    auto prefs = ThePrefs;
    prefs.DrivePath[0] = path;
    prefs.Emul1541Proc = emul_1541_proc;

    MII_DEBUG_PRINTF("MountDrive8: calling NewPrefs (old Emul1541Proc=%d)\n", ThePrefs.Emul1541Proc);
    NewPrefs(&prefs);
    ThePrefs = prefs;

    MII_DEBUG_PRINTF("MountDrive8: done, ThePrefs.Emul1541Proc=%d, TheCPU1541->Idle=%d\n",
           ThePrefs.Emul1541Proc, TheCPU1541->Idle);
}

void C64::UnmountDrive8()
{
    MII_DEBUG_PRINTF("UnmountDrive8\n");

    auto prefs = ThePrefs;

    // ВАЖНО: пустая строка, не nullptr
    prefs.DrivePath[0].clear();

    // сохранить режим эмуляции
    prefs.Emul1541Proc = ThePrefs.Emul1541Proc;

    NewPrefs(&prefs);
    ThePrefs = prefs;

    // сбросить состояние 1541 / IEC
    TheCPU1541->AsyncReset();
    TheGCRDisk->Reset();
    TheIEC->Reset();
}

void C64::MountDrive1(const char *path)
{
    // Tape not supported on RP2350
}


void C64::InsertCartridge(const std::string &path)
{
    MII_DEBUG_PRINTF("InsertCartridge: %s\n", path.c_str());

    if (path.empty()) {
        // Remove cartridge
        delete TheCart;
        TheCart = new NoCartridge;
        TheCPU->SetChips(TheVIC, TheSID, TheCIA1, TheCIA2, TheCart, TheIEC, TheTape);
        ShowNotification("Cartridge removed");
        return;
    }

    // Load cartridge from file
    std::string error;
    Cartridge *new_cart = Cartridge::FromFile(path, error);

    if (new_cart) {
        // Swap cartridge
        delete TheCart;
        TheCart = new_cart;
        TheCPU->SetChips(TheVIC, TheSID, TheCIA1, TheCIA2, TheCart, TheIEC, TheTape);
        ShowNotification("Cartridge inserted");
        MII_DEBUG_PRINTF("Cartridge loaded successfully\n");

        // Reset C64 to start cartridge
        Reset(false);
    } else {
        MII_DEBUG_PRINTF("Failed to load cartridge: %s\n", error.c_str());
        ShowNotification(error);
    }
}


/*
 *  Set play mode
 */

void C64::SetPlayMode(PlayMode mode)
{
    play_mode = mode;
}


/*
 *  Tape controls (stubs - tape not supported on RP2350)
 */

void C64::SetTapeButtons(TapeState pressed) {}
void C64::SetTapeControllerButton(bool pressed) {}
void C64::RewindTape() {}
void C64::ForwardTape() {}
TapeState C64::TapeButtonState() const { return TapeState::Stop; }
TapeState C64::TapeDriveState() const { return TapeState::Stop; }
int C64::TapePosition() const { return 0; }


/*
 *  Drive LEDs and notifications
 */

void C64::SetDriveLEDs(int l0, int l1, int l2, int l3)
{
    TheDisplay->SetLEDs(l0, l1, l2, l3);
}


void C64::ShowNotification(std::string s)
{
    TheDisplay->ShowNotification(s);
}


/*
 *  Snapshot functions (stubs - not implemented on RP2350)
 */

void C64::MakeSnapshot(Snapshot *s, bool instruction_boundary) {}
void C64::RestoreSnapshot(const Snapshot *s) {}
bool C64::SaveSnapshot(const std::string &filename, std::string &ret_error_msg)
{
    ret_error_msg = "Not supported on RP2350";
    return false;
}
bool C64::LoadSnapshot(const std::string &filename, Prefs *prefs, std::string &ret_error_msg)
{
    ret_error_msg = "Not supported on RP2350";
    return false;
}


/*
 *  DMA Load (direct memory load)
 */

bool C64::DMALoad(const std::string &filename, std::string &ret_error_msg)
{
    ret_error_msg = "Use c64_load_prg() instead";
    return false;
}


void C64::AutoStartOp() {}


/*
 *  Swap cartridge (simplified)
 */

void C64::swap_cartridge(int oldreu, const std::string &oldcart, int newreu, const std::string &newcart)
{
    // Simplified for RP2350 - no runtime cartridge swapping
}


/*
 *  Keycode functions
 */

int KeycodeFromString(const std::string &s) { return -1; }
const char *StringForKeycode(unsigned kc) { return ""; }
bool IsSnapshotFile(const char *filename) { return false; }


//=============================================================================
// C Interface Functions (called from main_rp2350.c)
//=============================================================================

extern "C" {

// Forward declarations
void c64_load_cartridge(const char *filename);

/*
 *  Initialize the C64 emulator
 */
void c64_init(void)
{
    MII_DEBUG_PRINTF("c64_init: Creating C64...\n");
    TheC64 = new C64();

    // Reset all chips
    TheC64->TheCPU->Reset();
    TheC64->TheSID->Reset();
    TheC64->TheCIA1->Reset();
    TheC64->TheCIA2->Reset();
    TheC64->TheCPU1541->Reset();
    TheC64->TheGCRDisk->Reset();

    MII_DEBUG_PRINTF("c64_init: C64 ready\n");
}


/*
 *  Reset the C64 emulator
 */
void c64_reset(void)
{
    if (TheC64) {
        TheC64->Reset(true);
    }
}


/*
 *  Trigger NMI (RESTORE key)
 */
void c64_nmi(void)
{
    if (TheC64) {
        TheC64->NMI();
    }
}


/*
 *  Run one frame of emulation
 *  Returns true when frame is complete
 */
bool c64_run_frame(void)
{
    if (!TheC64) {
        return false;
    }

    C64 *c64 = TheC64;

    // Poll input BEFORE frame emulation so games see current joystick state
    c64->TheCIA1->Joystick1 = 0xff;
    c64->TheCIA1->Joystick2 = 0xff;
    c64->TheDisplay->PollKeyboard(c64->TheCIA1->KeyMatrix, c64->TheCIA1->RevMatrix, &c64->joykey);

    // Apply both joystick states to both C64 ports
    // F9 swaps which physical gamepad controls which port (handled in input_rp2350.cpp)
    // Port 1 = Joystick1 ($DC01), Port 2 = Joystick2 ($DC00)
    extern uint8_t input_get_joystick2(void);
    c64->TheCIA1->Joystick1 &= c64->joykey;           // Port 1 from joystick1
    c64->TheCIA1->Joystick2 &= input_get_joystick2(); // Port 2 from joystick2

    // Run one frame's worth of emulation (line-based)
    bool frame_complete = false;
    int line_count = 0;
    const int MAX_LINES_PER_FRAME = 400;  // Safety limit (PAL has 312 lines)

    while (!frame_complete && line_count < MAX_LINES_PER_FRAME) {
        // Feed watchdog every 50 lines to prevent timeout during long frames
        // DISABLED for debugging
        // if ((line_count & 0x3F) == 0) {
        //     watchdog_update();
        // }

        // Emulate one raster line
        int cycles_left = 0;
        unsigned vic_flags = c64->TheVIC->EmulateLine(cycles_left);

        // SID emulation (audio samples)
        c64->TheSID->EmulateLine();

#if !PRECISE_CIA_CYCLES
        // CIA timers
        c64->TheCIA1->EmulateLine(ThePrefs.CIACycles);
        c64->TheCIA2->EmulateLine(ThePrefs.CIACycles);
#endif

        // CPU emulation
        // Frodo's $f2 opcode mechanism handles IEC traps internally
        c64->TheCPU->EmulateLine(cycles_left);
        c64->cycle_counter += CYCLES_PER_LINE;

        line_count++;

        // Check for VBlank (end of frame)
        if (vic_flags & VIC_VBLANK) {
            frame_complete = true;

            // Count TOD clocks
            c64->TheCIA1->CountTOD();
            c64->TheCIA2->CountTOD();
        }
    }
#if 0
    // Debug: warn if we hit the safety limit
    if (line_count >= MAX_LINES_PER_FRAME) {
        static int overflow_count = 0;
        if (++overflow_count <= 5) {
            MII_DEBUG_PRINTF("WARNING: Frame exceeded %d lines!\n", MAX_LINES_PER_FRAME);
        }
    }

    // Periodic debug: print memory/state info every 500 frames
    static uint32_t debug_frame_count = 0;
    debug_frame_count++;
    if ((debug_frame_count % 500) == 0) {
        MII_DEBUG_PRINTF("Frame %lu: lines=%d, PC=$%04X, 1541: %s, Idle=%d\n",
               (unsigned long)debug_frame_count, line_count,
               c64->TheCPU->GetPC(),
               ThePrefs.Emul1541Proc ? "ON" : "OFF",
               c64->TheCPU1541->Idle);
    }
#endif
    return true;
}


/*
 *  Get pointer to VIC framebuffer
 */
uint8_t *c64_get_framebuffer(void)
{
    if (g_display) {
        return g_display->GetFramebuffer();
    }
    return nullptr;
}


/*
 *  Get pointer to C64 RAM
 */
uint8_t *c64_get_ram(void)
{
    return TheC64 ? TheC64->RAM : nullptr;
}


/*
 *  Set drive LEDs
 */
void c64_set_drive_leds(int l0, int l1, int l2, int l3)
{
    if (TheC64) {
        TheC64->SetDriveLEDs(l0, l1, l2, l3);
    }
}


/*
 *  Show notification message
 */
void c64_show_notification(const char *msg)
{
    if (TheC64) {
        TheC64->ShowNotification(msg);
    }
}


/*
 *  Mount a disk image using DOS-level IEC emulation
 *  This uses Frodo's built-in IEC class with ImageDrive
 */
void c64_mount_disk(const uint8_t *data, uint32_t size, const char *filename)
{
    (void)data;
    (void)size;

    MII_DEBUG_PRINTF("c64_mount_disk: %s\n", filename);

    // Use Frodo's built-in DOS-level IEC emulation
    // false = DOS-level emulation (not processor-level 1541)
    TheC64->MountDrive8(false, filename);

    MII_DEBUG_PRINTF("c64_mount_disk: mounted via Frodo IEC (Emul1541Proc=%d)\n",
           ThePrefs.Emul1541Proc);
}

void c64_unmount_disk(void) {
    if (TheC64)
        TheC64->UnmountDrive8();
}

void c64_eject_cartridge(void)
{
    if (!TheC64) return;
    TheC64->InsertCartridge("");
}

/*
 *  Load a PRG file directly into RAM
 */
bool c64_load_prg_from_file(FIL *file)
{
    if (!TheC64 || !TheC64->RAM || !file)
        return false;

    UINT br;

    // 1. Read load address
    uint8_t hdr[2];
    if (f_read(file, hdr, 2, &br) != FR_OK || br != 2)
        return false;

    uint16_t load_addr = hdr[0] | (hdr[1] << 8);

    FSIZE_t file_size = f_size(file);
    if (file_size < 3)
        return false;

    uint32_t prg_size = file_size - 2;

    // 2. Clamp to RAM
    if (load_addr >= C64_RAM_SIZE)
        return false;

    if (load_addr + prg_size > C64_RAM_SIZE)
        prg_size = C64_RAM_SIZE - load_addr;

    MII_DEBUG_PRINTF(
        "c64_load_prg: Loading %lu bytes at $%04X\n",
        (unsigned long)prg_size, load_addr
    );

    // 3. Read directly into C64 RAM
    uint8_t *dst = TheC64->RAM + load_addr;
    uint32_t remaining = prg_size;

    while (remaining) {
        UINT chunk = remaining > 512 ? 512 : remaining;

        if (f_read(file, dst, chunk, &br) != FR_OK || br != chunk)
            return false;

        dst += chunk;
        remaining -= chunk;
    }

    // 4. BASIC pointers if $0801
    if (load_addr == 0x0801) {
        uint16_t end_addr = load_addr + prg_size;

        TheC64->RAM[0x2d] = end_addr & 0xff;  // VARTAB
        TheC64->RAM[0x2e] = end_addr >> 8;
        TheC64->RAM[0x2f] = TheC64->RAM[0x2d]; // ARYTAB
        TheC64->RAM[0x30] = TheC64->RAM[0x2e];
        TheC64->RAM[0x31] = TheC64->RAM[0x2d]; // STREND
        TheC64->RAM[0x32] = TheC64->RAM[0x2e];
    }

    return true;
}

/*
 *  Type a string into the C64 keyboard buffer
 *  The C64 keyboard buffer is at $0277-$0280 (10 bytes max)
 *  Buffer length is at $C6
 */
void c64_type_string(const char *str)
{
    if (!TheC64 || !TheC64->RAM || !str) return;

    int len = strlen(str);
    if (len > 10) len = 10;  // C64 keyboard buffer is 10 bytes max

    // Write to keyboard buffer at $0277
    for (int i = 0; i < len; i++) {
        TheC64->RAM[0x0277 + i] = str[i];
    }

    // Set buffer length at $C6
    TheC64->RAM[0xC6] = len;

    MII_DEBUG_PRINTF("c64_type_string: queued %d chars\n", len);
}

/*
 *  Load a PRG/D64/G64/D81 file from SD card
 *  For PRG: loads into RAM and queues RUN
 *  For D64/G64/D81: mounts as disk drive and queues LOAD"*",8,1
 */
void c64_load_file(const char *filename)
{
    if (!filename)
        return;

    MII_DEBUG_PRINTF("c64_load_file: %s\n", filename);

    const char *ext = strrchr(filename, '.');
    if (!ext) {
        MII_DEBUG_PRINTF("No file extension\n");
        return;
    }

    // ---------- PRG ----------
    if (strcasecmp(ext, ".prg") == 0) {
        FIL file;
        if (f_open(&file, filename, FA_READ) != FR_OK) {
            MII_DEBUG_PRINTF("Failed to open PRG file\n");
            return;
        }

        bool ok = c64_load_prg_from_file(&file);
        f_close(&file);

        if (!ok) {
            MII_DEBUG_PRINTF("Failed to load PRG\n");
            return;
        }

        // Auto-RUN
        c64_type_string("RUN\r");
        return;
    }

    // ---------- DISK ----------
    if (strcasecmp(ext, ".d64") == 0 ||
        strcasecmp(ext, ".g64") == 0 ||
        strcasecmp(ext, ".d81") == 0) {

        c64_mount_disk(NULL, 0, filename);

        // LOAD"*",8,1 via abbreviation
        c64_type_string("L\xCF\"*\",8,1\r");
        return;
    }

    // ---------- CARTRIDGE ----------
    if (strcasecmp(ext, ".crt") == 0) {
        c64_load_cartridge(filename);
        if (TheC64)
            TheC64->ResetAndAutoStart();
        return;
    }

    MII_DEBUG_PRINTF("Unsupported file type: %s\n", ext);
}

/*
 *  Load a CRT cartridge file
 */
void c64_load_cartridge(const char *filename)
{
    if (!TheC64 || !filename) return;

    MII_DEBUG_PRINTF("c64_load_cartridge: %s\n", filename);

    TheC64->InsertCartridge(filename);
}

}  // extern "C"
