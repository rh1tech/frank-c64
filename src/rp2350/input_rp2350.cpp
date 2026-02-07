/*
 *  input_rp2350.cpp - Input handling for RP2350
 *
 *  MurmC64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Handles PS/2 keyboard, USB HID keyboard, and NES/SNES gamepad input.
 */

#include "board_config.h"

extern "C" {
#include "debug_log.h"
#include "pico/stdlib.h"
#include "nespad/nespad.h"

#if ENABLE_PS2_KEYBOARD
#include "ps2kbd/ps2kbd_wrapper.h"
#endif

#ifdef USB_HID_ENABLED
#include "usbhid/usbhid_wrapper.h"
#include "usbhid/usbhid.h"
#endif

// Start screen
#include "startscreen.h"

// Disk UI functions
void disk_ui_init(void);
void disk_ui_show(void);
void disk_ui_hide(void);
void disk_ui_delete(void);
bool disk_ui_is_visible(void);
void disk_ui_move_up(void);
void disk_ui_move_down(void);
int disk_ui_get_selected(void);
void disk_ui_select(void);
int disk_ui_get_state(void);
void disk_ui_action_up(void);
void disk_ui_action_down(void);
int disk_ui_get_action(void);
void disk_ui_confirm_action(void);
void disk_ui_cancel_action(void);

// Disk UI states (must match disk_ui.h)
#define DISK_UI_HIDDEN        0
#define DISK_UI_SELECT_FILE   1
#define DISK_UI_SELECT_ACTION 2

// Disk loader functions
const char *disk_loader_get_path(int index);
void c64_mount_disk(const uint8_t *data, uint32_t size, const char *filename);
void c64_load_file(const char *filename);  // Load file and auto-run (D64/PRG/CRT)
void c64_load_cartridge(const char *filename);  // Load CRT cartridge
void c64_unmount_disk(void);
void c64_eject_cartridge(void);
}

#include <cstring>

//=============================================================================
// C64 Keyboard Matrix Mapping
//=============================================================================

/*
  C64 keyboard matrix:

    Bit 7   6   5   4   3   2   1   0
  0    CUD  F5  F3  F1  F7 CLR RET DEL
  1    SHL  E   S   Z   4   A   W   3
  2     X   T   F   C   6   D   R   5
  3     V   U   H   B   8   G   Y   7
  4     N   O   K   M   0   J   I   9
  5     ,   @   :   .   -   L   P   +
  6     /   ↑   =  SHR HOM  ;   *   £
  7    R/S  Q   C= SPC  2  CTL  ←   1
*/

#define MATRIX(a, b) (((a) << 3) | (b))

//=============================================================================
// PS/2 Scancode to C64 Matrix Translation
//=============================================================================

// PS/2 Set 2 scancodes -> C64 matrix position
// Format: PS/2 scancode (with 0xF0 prefix for break) -> C64 matrix byte:bit
static const int8_t ps2_to_c64[128] = {
    -1,     // 0x00
    -1,     // 0x01 F9
    -1,     // 0x02
    -1,     // 0x03 F5 (handled separately)
    -1,     // 0x04 F3 (handled separately)
    -1,     // 0x05 F1 (handled separately)
    -1,     // 0x06 F2
    -1,     // 0x07 F12
    -1,     // 0x08
    -1,     // 0x09 F10
    -1,     // 0x0A F8
    -1,     // 0x0B F6
    -1,     // 0x0C F4
    MATRIX(7, 2),  // 0x0D Tab -> CTRL
    MATRIX(7, 1),  // 0x0E ` -> ←
    -1,     // 0x0F

    -1,     // 0x10
    MATRIX(7, 5),  // 0x11 L-Alt -> C=
    MATRIX(1, 7),  // 0x12 L-Shift
    -1,     // 0x13
    MATRIX(7, 2),  // 0x14 L-Ctrl
    MATRIX(7, 6),  // 0x15 Q
    MATRIX(7, 0),  // 0x16 1
    -1,     // 0x17
    -1,     // 0x18
    -1,     // 0x19
    MATRIX(1, 4),  // 0x1A Z
    MATRIX(1, 5),  // 0x1B S
    MATRIX(1, 2),  // 0x1C A
    MATRIX(1, 1),  // 0x1D W
    MATRIX(7, 3),  // 0x1E 2
    -1,     // 0x1F

    -1,     // 0x20
    MATRIX(2, 4),  // 0x21 C
    MATRIX(2, 7),  // 0x22 X
    MATRIX(2, 2),  // 0x23 D
    MATRIX(1, 6),  // 0x24 E
    MATRIX(1, 3),  // 0x25 4
    MATRIX(1, 0),  // 0x26 3
    -1,     // 0x27
    -1,     // 0x28
    MATRIX(7, 4),  // 0x29 Space
    MATRIX(3, 7),  // 0x2A V
    MATRIX(2, 5),  // 0x2B F
    MATRIX(2, 6),  // 0x2C T
    MATRIX(2, 1),  // 0x2D R
    MATRIX(2, 0),  // 0x2E 5
    -1,     // 0x2F

    -1,     // 0x30
    MATRIX(4, 7),  // 0x31 N
    MATRIX(3, 4),  // 0x32 B
    MATRIX(3, 5),  // 0x33 H
    MATRIX(3, 2),  // 0x34 G
    MATRIX(3, 1),  // 0x35 Y
    MATRIX(2, 3),  // 0x36 6
    -1,     // 0x37
    -1,     // 0x38
    -1,     // 0x39
    MATRIX(4, 4),  // 0x3A M
    MATRIX(4, 2),  // 0x3B J
    MATRIX(3, 6),  // 0x3C U
    MATRIX(3, 0),  // 0x3D 7
    MATRIX(3, 3),  // 0x3E 8
    -1,     // 0x3F

    -1,     // 0x40
    MATRIX(5, 7),  // 0x41 ,
    MATRIX(4, 5),  // 0x42 K
    MATRIX(4, 1),  // 0x43 I
    MATRIX(4, 6),  // 0x44 O
    MATRIX(4, 3),  // 0x45 0
    MATRIX(4, 0),  // 0x46 9
    -1,     // 0x47
    -1,     // 0x48
    MATRIX(5, 4),  // 0x49 .
    MATRIX(6, 7),  // 0x4A /
    MATRIX(5, 2),  // 0x4B L
    MATRIX(5, 5),  // 0x4C ;/:
    MATRIX(5, 1),  // 0x4D P
    MATRIX(5, 3),  // 0x4E -
    -1,     // 0x4F

    -1,     // 0x50
    -1,     // 0x51
    MATRIX(6, 2),  // 0x52 '
    -1,     // 0x53
    MATRIX(5, 6),  // 0x54 [/@
    MATRIX(5, 0),  // 0x55 =/+
    -1,     // 0x56
    -1,     // 0x57
    -1,     // 0x58 Caps Lock
    MATRIX(6, 4),  // 0x59 R-Shift
    MATRIX(0, 1),  // 0x5A Enter
    MATRIX(6, 1),  // 0x5B ]/*
    -1,     // 0x5C
    MATRIX(6, 0),  // 0x5D \ -> £
    -1,     // 0x5E
    -1,     // 0x5F

    -1,     // 0x60
    -1,     // 0x61
    -1,     // 0x62
    -1,     // 0x63
    -1,     // 0x64
    -1,     // 0x65
    MATRIX(0, 0),  // 0x66 Backspace -> DEL
    -1,     // 0x67
    -1,     // 0x68
    -1,     // 0x69 KP 1
    -1,     // 0x6A
    -1,     // 0x6B KP 4
    -1,     // 0x6C KP 7
    -1,     // 0x6D
    -1,     // 0x6E
    -1,     // 0x6F

    -1,     // 0x70 KP 0
    -1,     // 0x71 KP .
    -1,     // 0x72 KP 2
    -1,     // 0x73 KP 5
    -1,     // 0x74 KP 6
    -1,     // 0x75 KP 8
    MATRIX(7, 7),  // 0x76 Escape -> RUN/STOP
    -1,     // 0x77 Num Lock
    -1,     // 0x78 F11
    -1,     // 0x79 KP +
    -1,     // 0x7A KP 3
    -1,     // 0x7B KP -
    -1,     // 0x7C KP *
    -1,     // 0x7D KP 9
    -1,     // 0x7E Scroll Lock
    -1,     // 0x7F
};

// Extended PS/2 scancodes (0xE0 prefix)
// Lookup function instead of designated initializer (not supported in C++)
static inline int8_t get_extended_c64_key(uint8_t scancode) {
    switch (scancode) {
        case 0x11: return MATRIX(7, 5);  // R-Alt -> C=
        case 0x14: return MATRIX(7, 2);  // R-Ctrl
        case 0x6B: return MATRIX(0, 2);  // Left arrow (with shift for cursor left)
        case 0x72: return MATRIX(0, 7);  // Down arrow
        case 0x74: return MATRIX(0, 2);  // Right arrow
        case 0x75: return MATRIX(0, 7);  // Up arrow (with shift)
        case 0x6C: return MATRIX(6, 3);  // Home -> CLR/HOME
        case 0x69: return MATRIX(6, 0);  // End -> £
        case 0x7D: return MATRIX(6, 6);  // Page Up -> ↑
        case 0x7A: return MATRIX(6, 5);  // Page Down -> =
        case 0x71: return MATRIX(0, 0);  // Delete -> INS/DEL
        default: return -1;
    }
}

// Map ASCII/special characters to C64 matrix position (VICE-style layout)
// Returns -1 if not mappable, or MATRIX(row, col) | 0x100 if shift needed
// Special codes from ps2kbd_wrapper:
//   0xE0 = <- (left arrow)
//   0xE1 = Caps Lock (shift lock)
//   0xE2 = ^ (up arrow)
//   0xE3 = Insert (shift+DEL)
//   0xE4 = Home (CLR/HOME)
//   0xE5 = End (£ pound)
//   0xF1-0xF8 = F1-F8
//   0xFB = F11 (RESTORE - handled separately)
//   0xFC = F12 (Reset - handled separately)
static int ascii_to_c64_matrix(unsigned char key) {
    switch (key) {
        // Letters (uppercase)
        case 'A': return MATRIX(1, 2);
        case 'B': return MATRIX(3, 4);
        case 'C': return MATRIX(2, 4);
        case 'D': return MATRIX(2, 2);
        case 'E': return MATRIX(1, 6);
        case 'F': return MATRIX(2, 5);
        case 'G': return MATRIX(3, 2);
        case 'H': return MATRIX(3, 5);
        case 'I': return MATRIX(4, 1);
        case 'J': return MATRIX(4, 2);
        case 'K': return MATRIX(4, 5);
        case 'L': return MATRIX(5, 2);
        case 'M': return MATRIX(4, 4);
        case 'N': return MATRIX(4, 7);
        case 'O': return MATRIX(4, 6);
        case 'P': return MATRIX(5, 1);
        case 'Q': return MATRIX(7, 6);
        case 'R': return MATRIX(2, 1);
        case 'S': return MATRIX(1, 5);
        case 'T': return MATRIX(2, 6);
        case 'U': return MATRIX(3, 6);
        case 'V': return MATRIX(3, 7);
        case 'W': return MATRIX(1, 1);
        case 'X': return MATRIX(2, 7);
        case 'Y': return MATRIX(3, 1);
        case 'Z': return MATRIX(1, 4);

        // Numbers
        case '1': return MATRIX(7, 0);
        case '2': return MATRIX(7, 3);
        case '3': return MATRIX(1, 0);
        case '4': return MATRIX(1, 3);
        case '5': return MATRIX(2, 0);
        case '6': return MATRIX(2, 3);
        case '7': return MATRIX(3, 0);
        case '8': return MATRIX(3, 3);
        case '9': return MATRIX(4, 0);
        case '0': return MATRIX(4, 3);

        // Punctuation (VICE positional layout)
        case ' ': return MATRIX(7, 4);   // Space
        case ',': return MATRIX(5, 7);   // ,
        case '.': return MATRIX(5, 4);   // .
        case '/': return MATRIX(6, 7);   // /
        case ';': return MATRIX(6, 5);   // ; (PC ' key -> C64 ;)
        case ':': return MATRIX(5, 5);   // : (PC ; key -> C64 :)
        case '=': return MATRIX(6, 5);   // = (Page Down -> =)
        case '+': return MATRIX(5, 0);   // + (PC - key -> C64 +)
        case '-': return MATRIX(5, 3);   // - (PC = key -> C64 -)
        case '*': return MATRIX(6, 1);   // * (PC ] key -> C64 *)
        case '@': return MATRIX(5, 6);   // @ (PC [ key -> C64 @)

        // Special keys
        case 0x0D: return MATRIX(0, 1);  // Enter/Return -> RETURN
        case 0x08: return MATRIX(0, 0);  // Backspace/Delete -> INS/DEL
        case 0x1B: return MATRIX(7, 7);  // Escape -> RUN/STOP
        case 0x09: return MATRIX(7, 2);  // Tab -> CTRL

        // Special C64 keys (codes from ps2kbd_wrapper)
        case 0xE0: return MATRIX(7, 1);  // <- (left arrow, PC ` key)
        case 0xE2: return MATRIX(6, 6);  // ^ (up arrow, PC \ key)
        case 0xE3: return MATRIX(0, 0) | 0x100;  // Insert -> Shift+INS/DEL
        case 0xE4: return MATRIX(6, 3);  // Home -> CLR/HOME
        case 0xE5: return MATRIX(6, 0);  // End -> £ (pound)

        // Arrow keys (directly mapped for cursor control)
        // Note: These are filtered when joystick mode is active
        case 0x15: return MATRIX(0, 2);  // Right arrow -> CRSR RIGHT
        case 0x0A: return MATRIX(0, 7);  // Down arrow -> CRSR DOWN
        case 0x0B: return MATRIX(0, 7) | 0x100;  // Up arrow -> CRSR UP (shift+down)

        // Function keys (C64 has F1, F3, F5, F7; F2, F4, F6, F8 need shift)
        case 0xF1: return MATRIX(0, 4);  // F1
        case 0xF2: return MATRIX(0, 4) | 0x100;  // F2 (F1+shift)
        case 0xF3: return MATRIX(0, 5);  // F3
        case 0xF4: return MATRIX(0, 5) | 0x100;  // F4 (F3+shift)
        case 0xF5: return MATRIX(0, 6);  // F5
        case 0xF6: return MATRIX(0, 6) | 0x100;  // F6 (F5+shift)
        case 0xF7: return MATRIX(0, 3);  // F7
        case 0xF8: return MATRIX(0, 3) | 0x100;  // F8 (F7+shift)

        default: return -1;
    }
}

//=============================================================================
// Input State
//=============================================================================

static struct {
    // C64 keyboard matrix state
    uint8_t key_matrix[8];
    uint8_t rev_matrix[8];

    // Joystick state (from gamepad)
    uint8_t joystick1;
    uint8_t joystick2;

    // Keyboard joystick emulation state
    // Arrow keys and R-Ctrl/R-Alt for joystick
    bool joy_up;
    bool joy_down;
    bool joy_left;
    bool joy_right;
    bool joy_fire;  // R-Ctrl or R-Alt

    // Joystick port selection (1 or 2, directly maps to C64 port)
    // Most games use port 2, but some use port 1
    int joy_port = 2;  // 1 or 2

    // PS/2 state
    bool ps2_extended;
    bool ps2_release;

    // Shift lock state (Caps Lock toggles this)
    bool shift_lock;

} input_state;

// External C64 control functions
extern "C" void c64_reset(void);
extern "C" void c64_nmi(void);

//=============================================================================
// Input Functions
//=============================================================================

extern "C" {

void input_rp2350_init(void)
{
    // Initialize key matrices (all keys released = all bits set)
    memset(input_state.key_matrix, 0xFF, sizeof(input_state.key_matrix));
    memset(input_state.rev_matrix, 0xFF, sizeof(input_state.rev_matrix));

    // Initialize joysticks (all directions/buttons released)
    input_state.joystick1 = 0x1F;
    input_state.joystick2 = 0x1F;

    // Default to joystick port 2 (most games use this)
    input_state.joy_port = 2;

    // Initialize gamepad
    nespad_begin(CPU_CLOCK_MHZ * 1000, NESPAD_GPIO_CLK, NESPAD_GPIO_DATA, NESPAD_GPIO_LATCH);

#if ENABLE_PS2_KEYBOARD
    // Initialize PS/2 keyboard
    MII_DEBUG_PRINTF("Initializing PS/2 keyboard on CLK=%d DATA=%d\n", PS2_PIN_CLK, PS2_PIN_DATA);
    ps2kbd_init();
#endif

#ifdef USB_HID_ENABLED
    // Initialize USB HID
    usbhid_wrapper_init();
#endif

    // Initialize disk UI
    disk_ui_init();

    MII_DEBUG_PRINTF("Input initialized\n");
}

static void process_ps2_scancode(uint8_t scancode)
{
    // Handle special prefixes
    if (scancode == 0xE0) {
        input_state.ps2_extended = true;
        return;
    }
    if (scancode == 0xF0) {
        input_state.ps2_release = true;
        return;
    }

    int c64_key = -1;
    bool add_shift = false;

    if (input_state.ps2_extended) {
        // Extended scancode
        if (scancode < 128) {
            c64_key = get_extended_c64_key(scancode);

            // Arrow keys need shift handling
            if (scancode == 0x6B || scancode == 0x75) {
                add_shift = true;  // Left and Up need shift
            }
        }
        input_state.ps2_extended = false;
    } else {
        // Regular scancode
        if (scancode < 128) {
            c64_key = ps2_to_c64[scancode];
        }

        // Handle function keys specially
        switch (scancode) {
            case 0x05: c64_key = MATRIX(0, 4); break;  // F1
            case 0x06: c64_key = MATRIX(0, 4); add_shift = true; break;  // F2
            case 0x04: c64_key = MATRIX(0, 5); break;  // F3
            case 0x0C: c64_key = MATRIX(0, 5); add_shift = true; break;  // F4
            case 0x03: c64_key = MATRIX(0, 6); break;  // F5
            case 0x0B: c64_key = MATRIX(0, 6); add_shift = true; break;  // F6
            case 0x83: c64_key = MATRIX(0, 3); break;  // F7
            case 0x0A: c64_key = MATRIX(0, 3); add_shift = true; break;  // F8
        }
    }

    if (c64_key < 0) {
        input_state.ps2_release = false;
        return;
    }

    int c64_byte = (c64_key >> 3) & 7;
    int c64_bit = c64_key & 7;

    if (input_state.ps2_release) {
        // Key released
        if (add_shift) {
            input_state.key_matrix[6] |= 0x10;  // Right shift
            input_state.rev_matrix[4] |= 0x40;
        }
        input_state.key_matrix[c64_byte] |= (1 << c64_bit);
        input_state.rev_matrix[c64_bit] |= (1 << c64_byte);
        input_state.ps2_release = false;
    } else {
        // Key pressed
        if (add_shift) {
            input_state.key_matrix[6] &= ~0x10;  // Right shift
            input_state.rev_matrix[4] &= ~0x40;
        }
        input_state.key_matrix[c64_byte] &= ~(1 << c64_bit);
        input_state.rev_matrix[c64_bit] &= ~(1 << c64_byte);
    }
}

// Helper to set/clear a key in the C64 matrix
static void set_c64_key(int c64_key, bool pressed) {
    if (c64_key < 0) return;

    bool need_shift = (c64_key & 0x100) != 0;
    c64_key &= 0xFF;

    int c64_byte = (c64_key >> 3) & 7;
    int c64_bit = c64_key & 7;

    if (pressed) {
        // Key pressed
        if (need_shift) {
            input_state.key_matrix[6] &= ~0x10;  // Right shift
            input_state.rev_matrix[4] &= ~0x40;
        }
        input_state.key_matrix[c64_byte] &= ~(1 << c64_bit);
        input_state.rev_matrix[c64_bit] &= ~(1 << c64_byte);
    } else {
        // Key released
        if (need_shift) {
            input_state.key_matrix[6] |= 0x10;  // Right shift
            input_state.rev_matrix[4] |= 0x40;
        }
        input_state.key_matrix[c64_byte] |= (1 << c64_bit);
        input_state.rev_matrix[c64_bit] |= (1 << c64_byte);
    }
}

// map NES pad state to C64 joystick format
inline static uint8_t map_nes_to_c64(uint32_t pad) {
    uint8_t joy = 0xFF;  // All released
    if ((pad & DPAD_UP) && (pad & DPAD_DOWN)) return joy;
    if ((pad & DPAD_LEFT) && (pad & DPAD_RIGHT)) return joy;
    if (pad & DPAD_UP)    joy &= ~0x01;  // Up
    if (pad & DPAD_DOWN)  joy &= ~0x02;  // Down
    if (pad & DPAD_LEFT)  joy &= ~0x04;  // Left
    if (pad & DPAD_RIGHT) joy &= ~0x08;  // Right
    if (pad & (DPAD_A | DPAD_B)) joy &= ~0x10;  // A or B -> Fire
    return joy;
}

void input_rp2350_poll(uint8_t *key_matrix, uint8_t *rev_matrix, uint8_t *joystick)
{
    constexpr uint8_t MOD_LSHIFT = 0x02;
    constexpr uint8_t MOD_RSHIFT = 0x20;
    static bool f9_was_pressed = false;
    static bool f11_was_pressed = false;
    uint8_t mods = 0;
#if ENABLE_PS2_KEYBOARD
    // Poll PS/2 keyboard
    ps2kbd_tick();
    int pressed;
    unsigned char key;
    while (ps2kbd_get_key(&pressed, &key)) {
        // F9 swaps gamepad port assignments
        // Default: Gamepad1 -> Port2, Gamepad2 -> Port1
        // Swapped: Gamepad1 -> Port1, Gamepad2 -> Port2
        if (key == 0xF9) {  // F9
            if (pressed && !f9_was_pressed) {
                input_state.joy_port = (input_state.joy_port == 1) ? 2 : 1;
                if (input_state.joy_port == 2) {
                    MII_DEBUG_PRINTF("Gamepads: Pad1->Port2, Pad2->Port1 (default)\n");
                } else {
                    MII_DEBUG_PRINTF("Gamepads: Pad1->Port1, Pad2->Port2 (swapped)\n");
                }
            }
            f9_was_pressed = pressed;
            continue;
        }

        // F10 toggles disk UI
        if (key == 0xFA) {  // F10
            static bool f10_was_pressed = false;
            if (pressed && !f10_was_pressed) {
                if (disk_ui_is_visible()) {
                    disk_ui_hide();
                } else {
                    disk_ui_show();
                }
            }
            f10_was_pressed = pressed;
            continue;
        }

        // F11 triggers RESTORE (NMI)
        if (key == 0xFB) {  // F11
            if (pressed && !f11_was_pressed) {
                MII_DEBUG_PRINTF("F11: RESTORE (NMI)\n");
                c64_nmi();
            }
            f11_was_pressed = pressed;
            continue;
        }

        // Caps Lock toggles shift lock
        if (key == 0xE1) {  // Caps Lock
            if (pressed) {
                input_state.shift_lock = !input_state.shift_lock;
                MII_DEBUG_PRINTF("Shift Lock: %s\n", input_state.shift_lock ? "ON" : "OFF");
            }
            continue;
        }

        // If disk UI is visible, handle navigation
        if (disk_ui_is_visible()) {
            if (pressed) {
                int state = disk_ui_get_state();

                if (state == DISK_UI_SELECT_FILE) {
                    // File selection mode
                    if (key == 0x0B || key == 0x52) {  // Up arrow
                        disk_ui_move_up();
                    } else if (key == 0x0A || key == 0x51) {  // Down arrow
                        disk_ui_move_down();
                    } else if (key == 0x0D) {  // Enter - show action menu
                        disk_ui_select();
                    } else if (key == 0x1B) {  // Escape - close UI
                        disk_ui_hide();
                    } else if (key == 'D') {
                        disk_ui_delete();
                    }
                } else if (state == DISK_UI_SELECT_ACTION) {
                    // Action selection mode
                    if (key == 0x0B || key == 0x52) {  // Up arrow
                        disk_ui_action_up();
                    } else if (key == 0x0A || key == 0x51) {  // Down arrow
                        disk_ui_action_down();
                    } else if (key == 0x0D) {  // Enter - confirm action
                        int sel = disk_ui_get_selected();
                        int action = disk_ui_get_action();
                        const char *path = disk_loader_get_path(sel);
                        if (path) {
                            c64_unmount_disk();
                            c64_eject_cartridge();
                            if (action == 0) {
                                // Load (run the disk/PRG)
                                MII_DEBUG_PRINTF("Loading disk: %s\n", path);
                                c64_load_file(path);
                            } else {
                                // Mount (just insert disk)
                                MII_DEBUG_PRINTF("Mounting disk: %s\n", path);
                                c64_mount_disk(NULL, 0, path);
                            }
                            disk_ui_confirm_action();
                        }
                    } else if (key == 0x1B) {  // Escape - back to file list
                        disk_ui_cancel_action();
                    }
                }
            }
            continue;  // Don't pass keys to C64 when UI is visible
        }

        // Filter out arrow keys - they're used for joystick emulation, not keyboard
        // Arrow key codes from ps2kbd: 0x15=right, 0x0A=down, 0x0B=up
        // Note: 0x08 is shared with backspace, so we don't filter it (left arrow uses joystick only)
        if (key == 0x15 || key == 0x0A || key == 0x0B) {
            continue;  // Skip arrow keys - used for joystick
        }

        int c64_key = ascii_to_c64_matrix(key);
        if (c64_key >= 0) {
            set_c64_key(c64_key, pressed != 0);
        }
    }

    // Handle shift key from modifiers and shift lock
    mods = ps2kbd_get_modifiers();
    {
        bool lshift = (mods & MOD_LSHIFT) || input_state.shift_lock;
        bool rshift = (mods & MOD_RSHIFT);
        // Left Shift (row 1, bit 7)
        if (lshift) {
            input_state.key_matrix[1] &= ~0x80;
            input_state.rev_matrix[7] &= ~0x02;
        } else {
            input_state.key_matrix[1] |= 0x80;
            input_state.rev_matrix[7] |= 0x02;
        }
        // Right Shift (row 6, bit 4)
        if (rshift) {
            input_state.key_matrix[6] &= ~0x10;
            input_state.rev_matrix[4] &= ~0x40;
        } else {
            input_state.key_matrix[6] |= 0x10;
            input_state.rev_matrix[4] |= 0x40;
        }
    }
    // Handle Ctrl key (L-Ctrl only - R-Ctrl is used for joystick fire)
    if (mods & 0x01) {  // L-Ctrl only
        input_state.key_matrix[7] &= ~0x04;  // CTRL
        input_state.rev_matrix[2] &= ~0x80;
    } else {
        input_state.key_matrix[7] |= 0x04;
        input_state.rev_matrix[2] |= 0x80;
    }

    // Handle C= key (L-Alt only - R-Alt is used for joystick fire)
    if (mods & 0x04) {  // L-Alt only
        input_state.key_matrix[7] &= ~0x20;  // C=
        input_state.rev_matrix[5] &= ~0x80;
    } else {
        input_state.key_matrix[7] |= 0x20;
        input_state.rev_matrix[5] |= 0x80;
    }

    // Ctrl+Alt+Delete triggers C64 reset
    static bool reset_combo_was_active = false;
    if (ps2kbd_is_reset_combo()) {
        if (!reset_combo_was_active) {
            MII_DEBUG_PRINTF("Ctrl+Alt+Del: C64 Reset\n");
            c64_unmount_disk();
            c64_eject_cartridge();
            c64_reset();
        }
        reset_combo_was_active = true;
    } else {
        reset_combo_was_active = false;
    }
#endif

#ifdef USB_HID_ENABLED
    // Process USB HID
    usbhid_wrapper_poll();

    int usb_pressed;
    unsigned char usb_key;
    while (usbhid_wrapper_get_key(&usb_pressed, &usb_key)) {
        if (usb_key == 0xF9) {  // F9
            if (pressed && !f9_was_pressed) {
                input_state.joy_port = (input_state.joy_port == 1) ? 2 : 1;
                if (input_state.joy_port == 2) {
                    MII_DEBUG_PRINTF("Gamepads: Pad1->Port2, Pad2->Port1 (default)\n");
                } else {
                    MII_DEBUG_PRINTF("Gamepads: Pad1->Port1, Pad2->Port2 (swapped)\n");
                }
            }
            f9_was_pressed = pressed;
            continue;
        }
        // F10 toggles disk UI
        if (usb_key == 0xFA) {  // F10
            static bool usb_f10_was_pressed = false;
            if (usb_pressed && !usb_f10_was_pressed) {
                if (disk_ui_is_visible()) {
                    disk_ui_hide();
                } else {
                    disk_ui_show();
                }
            }
            usb_f10_was_pressed = usb_pressed;
            continue;
        }

        // F11 triggers RESTORE (NMI)
        if (usb_key == 0xFB) {  // F11
            static bool usb_f11_was_pressed = false;
            if (usb_pressed && !usb_f11_was_pressed) {
                MII_DEBUG_PRINTF("F11: RESTORE (NMI)\n");
                c64_nmi();
            }
            usb_f11_was_pressed = usb_pressed;
            continue;
        }

        // Caps Lock toggles shift lock
        if (usb_key == 0xE1) {  // Caps Lock
            if (usb_pressed) {
                input_state.shift_lock = !input_state.shift_lock;
                MII_DEBUG_PRINTF("Shift Lock: %s\n", input_state.shift_lock ? "ON" : "OFF");
            }
            continue;
        }

        // If disk UI is visible, handle navigation
        if (disk_ui_is_visible()) {
            if (usb_pressed) {
                int state = disk_ui_get_state();

                if (state == DISK_UI_SELECT_FILE) {
                    // File selection mode
                    if (usb_key == 0x0B || usb_key == 0x52) {  // Up arrow
                        disk_ui_move_up();
                    } else if (usb_key == 0x0A || usb_key == 0x51) {  // Down arrow
                        disk_ui_move_down();
                    } else if (usb_key == 0x0D) {  // Enter - show action menu
                        disk_ui_select();
                    } else if (usb_key == 0x1B) {  // Escape - close UI
                        disk_ui_hide();
                    }
                } else if (state == DISK_UI_SELECT_ACTION) {
                    // Action selection mode
                    if (usb_key == 0x0B || usb_key == 0x52) {  // Up arrow
                        disk_ui_action_up();
                    } else if (usb_key == 0x0A || usb_key == 0x51) {  // Down arrow
                        disk_ui_action_down();
                    } else if (usb_key == 0x0D) {  // Enter - confirm action
                        int sel = disk_ui_get_selected();
                        int action = disk_ui_get_action();
                        const char *path = disk_loader_get_path(sel);
                        if (path) {
                            if (action == 0) {
                                // Load (run the disk/PRG)
                                MII_DEBUG_PRINTF("Loading disk: %s\n", path);
                                c64_load_file(path);
                            } else {
                                // Mount (just insert disk)
                                MII_DEBUG_PRINTF("Mounting disk: %s\n", path);
                                c64_mount_disk(NULL, 0, path);
                            }
                            disk_ui_confirm_action();
                        }
                    } else if (usb_key == 0x1B) {  // Escape - back to file list
                        disk_ui_cancel_action();
                    }
                }
            }
            continue;
        }

        int c64_key = ascii_to_c64_matrix(usb_key);
        if (c64_key >= 0) {
            set_c64_key(c64_key, usb_pressed != 0);
        }
    }

    // Handle shift key from USB modifiers and shift lock
    uint8_t usb_mods = mods | usbhid_wrapper_get_modifiers();
    {
        bool lshift = (usb_mods & MOD_LSHIFT) || input_state.shift_lock;
        bool rshift = (usb_mods & MOD_RSHIFT);

        // Left Shift (row 1, bit 7)
        if (lshift) {
            input_state.key_matrix[1] &= ~0x80;
            input_state.rev_matrix[7] &= ~0x02;
        } else {
            input_state.key_matrix[1] |= 0x80;
            input_state.rev_matrix[7] |= 0x02;
        }

        // Right Shift (row 6, bit 4)
        if (rshift) {
            input_state.key_matrix[6] &= ~0x10;
            input_state.rev_matrix[4] &= ~0x40;
        } else {
            input_state.key_matrix[6] |= 0x10;
            input_state.rev_matrix[4] |= 0x40;
        }
    }

    // Handle Ctrl key (L-Ctrl only - R-Ctrl is used for joystick fire)
    if (usb_mods & 0x01) {  // L-Ctrl only
        input_state.key_matrix[7] &= ~0x04;  // CTRL
        input_state.rev_matrix[2] &= ~0x80;
    } else {
        input_state.key_matrix[7] |= 0x04;
        input_state.rev_matrix[2] |= 0x80;
    }

    // Handle C= key (L-Alt only - R-Alt is used for joystick fire)
    if (usb_mods & 0x04) {  // L-Alt only
        input_state.key_matrix[7] &= ~0x20;  // C=
        input_state.rev_matrix[5] &= ~0x80;
    } else {
        input_state.key_matrix[7] |= 0x20;
        input_state.rev_matrix[5] |= 0x80;
    }
#endif

    // Poll both NES gamepads
    nespad_read();

    // Map NES pad to C64 joystick format
    // NES button masks (from nespad.h):
    //   DPAD_UP=0x000100, DPAD_DOWN=0x000400, DPAD_LEFT=0x001000, DPAD_RIGHT=0x004000
    //   DPAD_A=0x000001, DPAD_B=0x000004, DPAD_START=0x000040, DPAD_SELECT=0x000010
    // C64 joystick: Up=0x01, Down=0x02, Left=0x04, Right=0x08, Fire=0x10

    // Map both NES gamepads
    uint8_t gamepad1_joy = map_nes_to_c64(nespad_state);
    uint8_t gamepad2_joy = map_nes_to_c64(nespad_state2);

#ifdef USB_HID_ENABLED
    // Merge USB gamepad input (active-low, so AND the values)
    // USB gamepad dpad: bit 0=up, 1=down, 2=left, 3=right
    // USB gamepad buttons: bit 0=A, 1=B, etc.
    if (usbhid_gamepad_connected()) {
        usbhid_gamepad_state_t usb_gp;
        usbhid_get_gamepad_state(&usb_gp);

        // Map USB gamepad to C64 joystick and merge with NES gamepad 1
        if (usb_gp.dpad & 0x01) gamepad1_joy &= ~0x01;  // Up
        if (usb_gp.dpad & 0x02) gamepad1_joy &= ~0x02;  // Down
        if (usb_gp.dpad & 0x04) gamepad1_joy &= ~0x04;  // Left
        if (usb_gp.dpad & 0x08) gamepad1_joy &= ~0x08;  // Right
        if (usb_gp.buttons & 0x03) gamepad1_joy &= ~0x10;  // A or B -> Fire
    }
#endif

    // Keyboard joystick emulation: arrow keys + R-Ctrl/R-Alt for fire
    // Applied to gamepad 1 position (primary player)
    // Only when disk UI is not visible
#if ENABLE_PS2_KEYBOARD
    if (!disk_ui_is_visible()) {
        // Arrow keys: ps2kbd_get_arrow_state() returns bits: 0=right, 1=left, 2=down, 3=up
        uint8_t arrows = ps2kbd_get_arrow_state();
        uint8_t mods = ps2kbd_get_modifiers();

        if (arrows & 0x08) gamepad1_joy &= ~0x01;  // Up (bit 3)
        if (arrows & 0x04) gamepad1_joy &= ~0x02;  // Down (bit 2)
        if (arrows & 0x02) gamepad1_joy &= ~0x04;  // Left (bit 1)
        if (arrows & 0x01) gamepad1_joy &= ~0x08;  // Right (bit 0)

        // R-Ctrl (0x10) or R-Alt (0x40) for fire button
        if (mods & 0x50) gamepad1_joy &= ~0x10;  // R-Ctrl=0x10, R-Alt=0x40
    }
#endif

#ifdef USB_HID_ENABLED
    // USB keyboard joystick emulation (same logic as PS/2)
    if (!disk_ui_is_visible()) {
        // Arrow keys: usbhid_wrapper_get_arrow_state() returns bits: 0=right, 1=left, 2=down, 3=up
        uint8_t arrows = usbhid_wrapper_get_arrow_state();
        uint8_t mods = usbhid_wrapper_get_modifiers();

        if (arrows & 0x08) gamepad1_joy &= ~0x01;  // Up (bit 3)
        if (arrows & 0x04) gamepad1_joy &= ~0x02;  // Down (bit 2)
        if (arrows & 0x02) gamepad1_joy &= ~0x04;  // Left (bit 1)
        if (arrows & 0x01) gamepad1_joy &= ~0x08;  // Right (bit 0)

        // R-Ctrl (0x10) or R-Alt (0x40) for fire button
        if (mods & 0x50) gamepad1_joy &= ~0x10;  // R-Ctrl=0x10, R-Alt=0x40
    }
#endif

    // Apply gamepad swap (F9 toggles joy_port between 1 and 2)
    // joy_port == 2 (default): Gamepad1 -> Port2 (Joystick2), Gamepad2 -> Port1 (Joystick1)
    // joy_port == 1 (swapped): Gamepad1 -> Port1 (Joystick1), Gamepad2 -> Port2 (Joystick2)
    if (input_state.joy_port == 2) {
        // Default: Gamepad 1 controls Port 2 (most single-player games use this)
        input_state.joystick2 = gamepad1_joy;  // Port 2 ($DC00)
        input_state.joystick1 = gamepad2_joy;  // Port 1 ($DC01)
    } else {
        // Swapped: Gamepad 1 controls Port 1
        input_state.joystick1 = gamepad1_joy;  // Port 1 ($DC01)
        input_state.joystick2 = gamepad2_joy;  // Port 2 ($DC00)
    }

    // Return state
    memcpy(key_matrix, input_state.key_matrix, 8);
    memcpy(rev_matrix, input_state.rev_matrix, 8);

    *joystick = input_state.joystick1;
}

// Get joystick 2 state (for port 1 emulation)
uint8_t input_get_joystick2(void)
{
    return input_state.joystick2;
}

// Get current joystick port (1 or 2)
int input_get_joy_port(void)
{
    return input_state.joy_port;
}

}  // extern "C"
