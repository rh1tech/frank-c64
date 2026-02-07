/*
 * ps2kbd_wrapper.cpp - PS/2 Keyboard wrapper for Apple IIe emulator
 * Simplified version without DOOM dependencies
 */

#include "../../src/board_config.h"
#include "ps2kbd_wrapper.h"
#include "ps2kbd_mrmltr.h"
#include <queue>

struct KeyEvent {
    int pressed;
    unsigned char key;
};

static std::queue<KeyEvent> event_queue;

// HID to C64 key mapping (VICE-style positional layout)
// Returns ASCII character or special code for C64 keyboard mapping
// Special return values:
//   0xF1-0xF8 = F1-F8 keys
//   0xF9 = F9 (unused)
//   0xFA = F10 (unused)
//   0xFB = F11 (RESTORE/disk UI)
//   0xFC = F12 (C64 Reset)
//   0xE0-0xEF = Special C64 keys (left arrow, up arrow, pound, etc.)
//
// VICE keyboard layout maps PC keys to C64 positions:
//   ` -> <- (left arrow)
//   - -> +
//   = -> -
//   [ -> @
//   ] -> *
//   \ -> ^ (up arrow)
//   ; -> :
//   ' -> ;
static unsigned char hid_to_c64(uint8_t code, uint8_t modifiers) {
    bool shift = (modifiers & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
    (void)shift;  // Shift is handled by C64 matrix, not here

    // Function keys - return special codes
    if (code >= 0x3A && code <= 0x45) {  // F1-F12
        return 0xF1 + (code - 0x3A);  // F1=0xF1, F2=0xF2, ... F11=0xFB, F12=0xFC
    }

    // Letters - always uppercase (C64 native)
    if (code >= 0x04 && code <= 0x1D) {
        return 'A' + (code - 0x04);
    }

    // Numbers (no shift handling - C64 matrix handles shift)
    if (code >= 0x1E && code <= 0x27) {
        static const char num_chars[] = "1234567890";
        int idx = (code == 0x27) ? 9 : (code - 0x1E);
        return num_chars[idx];
    }

    // Special keys
    switch (code) {
        case 0x28: return 0x0D;  // Enter -> RETURN
        case 0x29: return 0x1B;  // Escape -> RUN/STOP
        case 0x2A: return 0x08;  // Backspace -> INS/DEL
        case 0x2B: return 0x09;  // Tab -> CTRL
        case 0x2C: return ' ';   // Space
        case 0x39: return 0xE1;  // Caps Lock -> SHIFT LOCK (special code)

        // VICE positional punctuation mapping
        case 0x2D: return '+';   // - key -> + (C64 plus)
        case 0x2E: return '-';   // = key -> - (C64 minus)
        case 0x2F: return '@';   // [ key -> @
        case 0x30: return '*';   // ] key -> *
        case 0x31: return 0xE6;  // \ key -> C64 = (special code)
        case 0x64: return 0xE6;  // Non-US \ key -> C64 '=' 
        case 0x32: return 0xE6;  // \ key -> C64 = (special code)
//        case 0x31: return 0xE2;  // \ key -> ^ (C64 up arrow, special code)
        case 0x33: return ':';   // ; key -> : (C64 colon)
        case 0x34: return ';';   // ' key -> ; (C64 semicolon)
        case 0x35: return 0xE0;  // ` key -> <- (C64 left arrow, special code)
        case 0x36: return ',';   // , key
        case 0x37: return '.';   // . key
        case 0x38: return '/';   // / key

        // Arrow keys (directly mapped, joystick filter handles these)
        case 0x4F: return 0x15;  // Right arrow
        case 0x50: return 0x08;  // Left arrow (same as backspace for cursor left)
        case 0x51: return 0x0A;  // Down arrow
        case 0x52: return 0x0B;  // Up arrow

        // Extended keys
        case 0x49: return 0xE3;  // Insert -> Shift+INS/DEL (special code)
        case 0x4C: return 0x08;  // Delete -> INS/DEL
        case 0x4A: return 0xE4;  // Home -> CLR/HOME (special code)
        case 0x4D: return 0xE5;  // End -> £ (pound, special code)
        case 0x4B: return 0xE2;  // Page Up -> ^ (up arrow)
        case 0x4E: return 0xE6;  // Page Down -> = (equals)

        default: return 0;
    }
}

static uint8_t current_modifiers = 0;

// Track raw HID arrow key state for joystick emulation
// Bits: 0=right, 1=left, 2=down, 3=up
static uint8_t arrow_key_state = 0;

// Track if Delete key is currently pressed (for Ctrl+Alt+Delete combo)
static bool delete_key_pressed = false;

static void key_handler(hid_keyboard_report_t *curr, hid_keyboard_report_t *prev) {
    // Store current modifiers for use in key mapping
    current_modifiers = curr->modifier;
    
    // Update arrow key state and Delete key from current report
    arrow_key_state = 0;
    delete_key_pressed = false;
    for (int i = 0; i < 6; i++) {
        if (curr->keycode[i] == 0x4F) arrow_key_state |= 0x01;  // Right
        if (curr->keycode[i] == 0x50) arrow_key_state |= 0x02;  // Left
        if (curr->keycode[i] == 0x51) arrow_key_state |= 0x04;  // Down
        if (curr->keycode[i] == 0x52) arrow_key_state |= 0x08;  // Up
        if (curr->keycode[i] == 0x4C) delete_key_pressed = true;  // Delete
    }
    
    // Check for key presses (in curr but not in prev)
    for (int i = 0; i < 6; i++) {
        if (curr->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (prev->keycode[j] == curr->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Key pressed
                unsigned char k = hid_to_c64(curr->keycode[i], curr->modifier);
                if (k) {
                    event_queue.push({1, k});
                }
            }
        }
    }
    
    // Check for key releases (in prev but not in curr)
    for (int i = 0; i < 6; i++) {
        if (prev->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (curr->keycode[j] == prev->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Key released
                unsigned char k = hid_to_c64(prev->keycode[i], prev->modifier);
                if (k) {
                    event_queue.push({0, k});
                }
            }
        }
    }
    
    // Handle Open Apple (Left Alt) and Solid Apple (Right Alt) as button events
    // These are handled separately by the emulator via modifier tracking
}

static Ps2Kbd_Mrmltr* kbd = nullptr;

void ps2kbd_init(void) {
    // Ps2Kbd_Mrmltr constructor takes (pio, gpio, keyHandler)
    static Ps2Kbd_Mrmltr kbd_instance(pio0, PS2_PIN_CLK, key_handler);
    kbd = &kbd_instance;
    kbd->init_gpio();
}

void ps2kbd_tick(void) {
    if (kbd) {
        kbd->tick();
    }
}

int ps2kbd_get_key(int* pressed, unsigned char* key) {
    if (event_queue.empty()) {
        return 0;
    }
    KeyEvent ev = event_queue.front();
    event_queue.pop();
    *pressed = ev.pressed;
    *key = ev.key;
    return 1;
}

// Get current modifier state (for Open Apple / Solid Apple buttons)
uint8_t ps2kbd_get_modifiers(void) {
    return current_modifiers;
}

// Get current arrow key state for joystick emulation
// Returns: bits 0=right, 1=left, 2=down, 3=up
uint8_t ps2kbd_get_arrow_state(void) {
    return arrow_key_state;
}

// Check if Ctrl+Alt+Delete is pressed (for system reset)
bool ps2kbd_is_reset_combo(void) {
    bool ctrl = (current_modifiers & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;
    bool alt = (current_modifiers & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT)) != 0;
    return ctrl && alt && delete_key_pressed;
}
