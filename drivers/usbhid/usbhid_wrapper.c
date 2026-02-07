/*
 * USB HID Wrapper for Apple IIe Emulator
 * Maps USB HID keyboard to Apple IIe key codes
 * Maps USB HID gamepad to NES-style button bits
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "usbhid.h"
#include "usbhid_wrapper.h"
#include <stdint.h>
#include <stdbool.h>
#include "nespad/nespad.h"

#ifdef USB_HID_ENABLED

// Track Delete key state for reset combo
static bool delete_key_pressed = false;
static uint8_t current_modifiers = 0;
static uint8_t current_arrow_state = 0;  // bits: 0=right, 1=left, 2=down, 3=up

//--------------------------------------------------------------------
// HID Keycode to C64 key mapping (same conventions as ps2kbd_wrapper.cpp)
// Returns ASCII character or special C64 code.
// Special return values:
//   0xF1-0xFC = F1-F12 (F11=0xFB, F12=0xFC)
//   0xE0-0xEF = Special C64 keys (left arrow, up arrow, shift lock, clr/home, pound, etc.)
//   0         = Ignored key
//--------------------------------------------------------------------
static unsigned char hid_to_c64(uint8_t hid_keycode, uint8_t modifiers) {
  
    // Function keys - F11 toggles disk selector
    if (hid_keycode >= 0x3A && hid_keycode <= 0x45) {
        return 0xF1 + (hid_keycode - 0x3A);  // F1=0xF1, ... F11=0xFB, F12=0xFC
    }
    
    // Letters A-Z - uppercase (C64 native)
    if (hid_keycode >= 0x04 && hid_keycode <= 0x1D) {
        return 'A' + (hid_keycode - 0x04);
    }
    
    // Numbers 1-9, 0 (no shift handling here)
    if (hid_keycode >= 0x1E && hid_keycode <= 0x27) {
        static const char num_chars[] = "1234567890";
        int idx = (hid_keycode == 0x27) ? 9 : (hid_keycode - 0x1E);
        return num_chars[idx];
    }
    
    // Special keys
    switch (hid_keycode) {
        case 0x28: return 0x0D;  // Enter -> RETURN
        case 0x29: return 0x1B;  // Escape -> RUN/STOP
        case 0x2A: return 0x08;  // Backspace -> INS/DEL
        case 0x2B: return 0x09;  // Tab -> CTRL
        case 0x2C: return ' ';   // Space
        case 0x39: return 0xE1;  // Caps Lock -> SHIFT LOCK (special code)

        // VICE positional punctuation mapping (match ps2 wrapper)
        case 0x2D: return '+';   // - key -> + (C64 plus)
        case 0x2E: return '-';   // = key -> - (C64 minus)
        case 0x2F: return '@';   // [ key -> @
        case 0x30: return '*';   // ] key -> *
        case 0x31: return 0xE6;   // \ key -> C64 = (special code)
        case 0x64: return 0xE6;  // Non-US \ key -> C64 '=' 
        case 0x32: return 0xE6;  // \ key -> C64 = (special code)
//        case 0x31: return 0xE2;  // \ key -> ^ (C64 up arrow, special code)
        case 0x33: return ':';   // ; key -> : (C64 colon)
        case 0x34: return ';';   // ' key -> ; (C64 semicolon)
        case 0x35: return 0xE0;  // ` key -> <- (C64 left arrow, special code)
        case 0x36: return ',';   // ,
        case 0x37: return '.';   // .
        case 0x38: return '/';   // /

        // Arrow keys (direct)
        case 0x4F: return 0x15;  // Right arrow (CTRL+U)
        case 0x50: return 0x08;  // Left arrow (same as INS/DEL in this mapping)
        case 0x51: return 0x0A;  // Down arrow (CTRL+J, line feed)
        case 0x52: return 0x0B;  // Up arrow (CTRL+K)

        // Extended keys
        case 0x49: return 0xE3;  // Insert -> Shift+INS/DEL (special code)
        case 0x4C: return 0x08;  // Delete -> INS/DEL
        case 0x4A: return 0xE4;  // Home -> CLR/HOME (special code)
        case 0x4D: return 0xE5;  // End -> £ (pound, special code)
        case 0x4B: return 0xE2;  // Page Up -> ^ (up arrow)
        case 0x4E: return 0xE6;  // Page Down -> = (equals)

        default: return 0;  // Unknown key
    }
}

//--------------------------------------------------------------------
// USB HID Wrapper API
//--------------------------------------------------------------------

void usbhid_wrapper_init(void) {
    usbhid_init();
    current_modifiers = 0;
    delete_key_pressed = false;
    current_arrow_state = 0;
}

void usbhid_wrapper_poll(void) {
    usbhid_task();

    // Update current modifier and Delete key state from keyboard
    usbhid_keyboard_state_t kbd_state;
    usbhid_get_keyboard_state(&kbd_state);
    current_modifiers = kbd_state.modifier;

    // Check special keys in current keycode slots
    delete_key_pressed = false;
    current_arrow_state = 0;

    for (int i = 0; i < 6; i++) {
        uint8_t kc = kbd_state.keycode[i];
        if (kc == 0x4C) {
            delete_key_pressed = true;
        }
        // Arrow keys: HID 0x4F=Right, 0x50=Left, 0x51=Down, 0x52=Up
        // Arrow state bits: 0=right, 1=left, 2=down, 3=up
        else if (kc == 0x4F) {
            current_arrow_state |= 0x01;  // Right
        }
        else if (kc == 0x50) {
            current_arrow_state |= 0x02;  // Left
        }
        else if (kc == 0x51) {
            current_arrow_state |= 0x04;  // Down
        }
        else if (kc == 0x52) {
            current_arrow_state |= 0x08;  // Up
        }
    }
}

int usbhid_wrapper_keyboard_connected(void) {
    return usbhid_keyboard_connected();
}

int usbhid_wrapper_gamepad_connected(void) {
    return usbhid_gamepad_connected();
}

int usbhid_wrapper_get_key(int *pressed, unsigned char *key) {
    uint8_t hid_keycode;
    int down;
    
    while (usbhid_get_key_action(&hid_keycode, &down)) {
        // Get current modifier state
        usbhid_keyboard_state_t kbd_state;
        usbhid_get_keyboard_state(&kbd_state);
        
        unsigned char c64_key = hid_to_c64(hid_keycode, kbd_state.modifier);
        if (c64_key != 0) {
            *pressed = down;
            *key = c64_key;
            return 1;
        }
    }
    
    return 0;
}

uint8_t usbhid_wrapper_get_modifiers(void) {
    return current_modifiers;
}

bool usbhid_wrapper_is_reset_combo(void) {
    // Check for Ctrl+Alt+Delete
    bool ctrl = (current_modifiers & 0x11) != 0;  // L/R Ctrl
    bool alt = (current_modifiers & 0x44) != 0;   // L/R Alt
    return ctrl && alt && delete_key_pressed;
}

uint8_t usbhid_wrapper_get_arrow_state(void) {
    return current_arrow_state;
}

uint32_t usbhid_wrapper_get_gamepad_state(void) {
    usbhid_gamepad_state_t gp;
    usbhid_get_gamepad_state(&gp);
    
    if (!gp.connected) {
        return 0;
    }
    
    uint32_t buttons = 0;
    
    // Map D-pad
    if (gp.dpad & 0x01) buttons |= DPAD_UP;
    if (gp.dpad & 0x02) buttons |= DPAD_DOWN;
    if (gp.dpad & 0x04) buttons |= DPAD_LEFT;
    if (gp.dpad & 0x08) buttons |= DPAD_RIGHT;
    
    // Map face buttons to NES-style
    // USB gamepad: A(0x01), B(0x02), X(0x04), Y(0x08), Start(0x40), Select(0x80)
    if (gp.buttons & 0x01) buttons |= DPAD_A;      // A -> A
    if (gp.buttons & 0x02) buttons |= DPAD_B;      // B -> B
    if (gp.buttons & 0x40) buttons |= DPAD_START;  // Start -> Start
    if (gp.buttons & 0x80) buttons |= DPAD_SELECT; // Select/Mode -> Select
    
    return buttons;
}

#endif // USB_HID_ENABLED
