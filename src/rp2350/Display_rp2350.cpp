/*
 *  Display_rp2350.cpp - C64 graphics display for RP2350
 *
 *  MurmC64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Implements Display class using HDMI output.
 */

#include "Display_rp2350.h"
#include "../board_config.h"

extern "C" {
#include "debug_log.h"
#include "pico/stdlib.h"
#include "HDMI.h"

// Input functions
void input_rp2350_poll(uint8_t *key_matrix, uint8_t *rev_matrix, uint8_t *joystick);

// Disk UI functions
bool disk_ui_is_visible(void);
}

#include <cstring>

// C64 Pepto color palette (same as murmc64)
static const uint32_t pepto_palette[16] = {
    0xFF000000,  // 0 Black
    0xFFFFFFFF,  // 1 White
    0xFF68372B,  // 2 Red
    0xFF70A4B2,  // 3 Cyan
    0xFF6F3D86,  // 4 Purple
    0xFF588D43,  // 5 Green
    0xFF352879,  // 6 Blue
    0xFFB8C76F,  // 7 Yellow
    0xFF6F4F25,  // 8 Orange
    0xFF433900,  // 9 Brown
    0xFF9A6759,  // 10 Light Red
    0xFF444444,  // 11 Dark Grey
    0xFF6C6C6C,  // 12 Grey
    0xFF9AD284,  // 13 Light Green
    0xFF6C5EB5,  // 14 Light Blue
    0xFF959595,  // 15 Light Grey
};

// Colodore palette (alternative)
static const uint32_t colodore_palette[16] = {
    0xFF000000,  // 0 Black
    0xFFFFFFFF,  // 1 White
    0xFF813338,  // 2 Red
    0xFF75CEC8,  // 3 Cyan
    0xFF8E3C97,  // 4 Purple
    0xFF56AC4D,  // 5 Green
    0xFF2E2C9B,  // 6 Blue
    0xFFEDF171,  // 7 Yellow
    0xFF8E5029,  // 8 Orange
    0xFF553800,  // 9 Brown
    0xFFC46C71,  // 10 Light Red
    0xFF4A4A4A,  // 11 Dark Grey
    0xFF7B7B7B,  // 12 Grey
    0xFFA9FF9F,  // 13 Light Green
    0xFF706DEB,  // 14 Light Blue
    0xFFB2B2B2,  // 15 Light Grey
};


/*
 *  Constructor
 */
extern "C" uint32_t __led_state;
uint32_t __led_state = 0;
static uint8_t* led_state = (uint8_t*)&__led_state; // Drive LED states
// Allocate VIC pixel buffer in SRAM (384 x 272 = 104448 bytes)
uint8_t g_pixels[DISPLAY_X * DISPLAY_Y];
// Source: VIC buffer (384x272, 8-bit indexed color)
// Dest: HDMI framebuffer (320x240, 8-bit indexed color)
extern "C" uint8_t* __not_in_flash() graphics_get_buffer_line(int y) {
    return g_pixels + C64_CROP_LEFT + (y + C64_CROP_TOP) * DISPLAY_X;
}

Display::Display(C64 * c64)
    : the_c64(c64), next_note(0), num_locked(false)
{
    vic_pixels = g_pixels;
    memset(vic_pixels, 0, DISPLAY_X * DISPLAY_Y);

    // Initialize LED states
    for (int i = 0; i < 4; i++) {
        led_state[i] = 0;
    }

    // Initialize notifications
    for (unsigned i = 0; i < NUM_NOTIFICATIONS; i++) {
        notes[i].active = false;
    }

    // Initialize speedometer
    memset(speedometer_string, 0, sizeof(speedometer_string));

    // Initialize color palette
    init_colors(PALETTE_PEPTO);

    MII_DEBUG_PRINTF("Display initialized: %dx%d\n", DISPLAY_X, DISPLAY_Y);
}


/*
 *  Destructor
 */

Display::~Display()
{
}


/*
 *  Initialize color palette
 */

void Display::init_colors(int palette_prefs)
{
    const uint32_t *src_palette;

    if (palette_prefs == PALETTE_COLODORE) {
        src_palette = colodore_palette;
    } else {
        src_palette = pepto_palette;
    }

    for (int i = 0; i < 16; i++) {
        palette[i] = src_palette[i];
    }
}


/*
 *  Pause display (no-op on RP2350)
 */

void Display::Pause()
{
    // Nothing to do
}


/*
 *  Resume display (no-op on RP2350)
 */

void Display::Resume()
{
    // Nothing to do
}


/*
 *  Preferences have changed
 */

void Display::NewPrefs(const Prefs *prefs)
{
    init_colors(prefs->Palette);
}


/*
 *  Return pointer to bitmap data ("active" VIC buffer)
 */

uint8_t * Display::BitmapBase()
{
    return vic_pixels;
}


/*
 *  Return number of bytes per row
 */

int Display::BitmapXMod()
{
    return DISPLAY_X;
}


/*
 *  Scale VIC output to HDMI framebuffer
 *
 *  VIC outputs 384x272 (DISPLAY_X x DISPLAY_Y)
 *  HDMI is 320x240 (FB_WIDTH x FB_HEIGHT)
 *
 *  We crop borders and scale slightly:
 *  - Horizontal: crop 32 pixels from left, use 320 pixels
 *  - Vertical: crop 16 lines from top, use 240 lines
 */

void Display::scale_to_hdmi()
{
#if 0
    if (!current_framebuffer) {
        return;
    }

    // Copy with cropping (no scaling, direct copy of center region)
    for (int y = 0; y < FB_HEIGHT; y++) {
        const uint8_t *src_row = g_pixels + C64_CROP_LEFT + (y + C64_CROP_TOP) * DISPLAY_X;
        uint8_t *dst_row = current_framebuffer + y * FB_WIDTH;

        // Copy 320 pixels
        memcpy(dst_row, src_row, FB_WIDTH);
    }
#endif
}


/*
 *  Draw overlays (LEDs, speedometer, notifications)
 */

void Display::draw_overlays()
{
#if 0
    if (!current_framebuffer) return;
    // Draw drive LEDs in top-right corner
    if (led_state[0] || led_state[1]) {
        // Simple LED indicator - draw a small rectangle
        int led_x = FB_WIDTH - 20;
        int led_y = 5;

        for (int dy = 0; dy < 6; dy++) {
            for (int dx = 0; dx < 12; dx++) {
                int idx = (led_y + dy) * FB_WIDTH + (led_x + dx);
                if (led_state[0] > 0) {
                    current_framebuffer[idx] = 5;  // Green for drive activity
                } else if (led_state[0] < 0) {
                    current_framebuffer[idx] = 2;  // Red for error
                }
            }
        }
    }

    // Draw notifications
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (unsigned i = 0; i < NUM_NOTIFICATIONS; i++) {
        if (notes[i].active) {
            // Check if notification has expired (3 seconds)
            if (now - notes[i].time > 3000) {
                notes[i].active = false;
                continue;
            }

            // Draw notification text at bottom of screen
            // (simplified - just mark area with a color)
            int note_y = FB_HEIGHT - 20 - i * 10;
            for (int dx = 0; dx < FB_WIDTH - 20; dx++) {
                int idx = note_y * FB_WIDTH + 10 + dx;
                current_framebuffer[idx] = 0;  // Black background
            }
        }
    }
#endif
}


/*
 *  Update display - copy VIC buffer to HDMI framebuffer
 */

void Display::Update()
{
}


/*
 *  Set LED states
 */

void Display::SetLEDs(int l0, int l1, int l2, int l3)
{
    led_state[0] = l0;
    led_state[1] = l1;
    led_state[2] = l2;
    led_state[3] = l3;
}


/*
 *  Set speedometer value
 */

void Display::SetSpeedometer(int speed)
{
    // Format speedometer string
    if (speed >= 100) {
        // At or above 100%, don't show
        speedometer_string[0] = '\0';
    } else {
        // Show percentage
        snprintf(speedometer_string, sizeof(speedometer_string), "%d%%", speed);
    }
}


/*
 *  Show notification message
 */

void Display::ShowNotification(std::string s)
{
    // Store notification
    Notification *note = &notes[next_note];
    next_note = (next_note + 1) % NUM_NOTIFICATIONS;

    // Copy text (truncate if necessary)
    size_t len = s.length();
    if (len >= NOTIFICATION_LENGTH) {
        len = NOTIFICATION_LENGTH - 1;
    }
    memcpy(note->text, s.c_str(), len);
    note->text[len] = '\0';

    // Set time and activate
    note->time = to_ms_since_boot(get_absolute_time());
    note->active = true;

    MII_DEBUG_PRINTF("Notification: %s\n", note->text);
}


/*
 *  Poll keyboard and joystick
 */

void Display::PollKeyboard(uint8_t *key_matrix, uint8_t *rev_matrix, uint8_t *joystick)
{
    // Call the RP2350 input handler
    input_rp2350_poll(key_matrix, rev_matrix, joystick);
}


/*
 *  Return NumLock state (for joystick keyboard swap)
 */

bool Display::NumLock()
{
    return num_locked;
}
