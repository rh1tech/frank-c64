/*
 * startscreen.c - Demoscene-style welcome screen for MurmC64
 *
 * Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Features:
 * - 250-color gradient palette
 * - Plasma effect background
 * - Copper/raster bars
 * - Smooth color transitions
 */

#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "startscreen.h"
#include "pico/stdlib.h"

// HDMI driver functions
extern uint8_t* graphics_get_buffer_line(int y);
extern void graphics_set_palette(uint8_t i, uint32_t color888);

// Screen dimensions
#define SCREEN_WIDTH   FB_WIDTH
#define SCREEN_HEIGHT  FB_HEIGHT

// UI dimensions
#define CHAR_WIDTH     6
#define CHAR_HEIGHT    8
#define LINE_HEIGHT    10

// Palette ranges for demoscene effects (~250 colors total)
#define PALETTE_PLASMA_START  16   // Start after C64 colors (0-15)
#define PALETTE_PLASMA_COUNT  218  // Plasma gradient (indices 16-233)
#define PALETTE_COPPER_START  234  // Copper bar colors (indices 234-249)
#define PALETTE_COPPER_COUNT  16   // Number of copper colors

// Text colors (using reserved entries 250-255)
#define COLOR_TEXT_WHITE   255
#define COLOR_TEXT_SHADOW  250
#define COLOR_TEXT_CYAN    251
#define COLOR_TEXT_YELLOW  252
#define COLOR_TEXT_GREEN   253

// Sine table for plasma effect (256 entries, values 0-255)
static const uint8_t sine_table[256] = {
    128,131,134,137,140,143,146,149,152,155,158,162,165,167,170,173,
    176,179,182,185,188,190,193,196,198,201,203,206,208,211,213,215,
    218,220,222,224,226,228,230,232,234,235,237,238,240,241,243,244,
    245,246,248,249,250,250,251,252,253,253,254,254,254,255,255,255,
    255,255,255,255,254,254,254,253,253,252,251,250,250,249,248,246,
    245,244,243,241,240,238,237,235,234,232,230,228,226,224,222,220,
    218,215,213,211,208,206,203,201,198,196,193,190,188,185,182,179,
    176,173,170,167,165,162,158,155,152,149,146,143,140,137,134,131,
    128,124,121,118,115,112,109,106,103,100,97,93,90,88,85,82,
    79,76,73,70,67,65,62,59,57,54,52,49,47,44,42,40,
    37,35,33,31,29,27,25,23,21,20,18,17,15,14,12,11,
    10,9,7,6,5,5,4,3,2,2,1,1,1,0,0,0,
    0,0,0,0,1,1,1,2,2,3,4,5,5,6,7,9,
    10,11,12,14,15,17,18,20,21,23,25,27,29,31,33,35,
    37,40,42,44,47,49,52,54,57,59,62,65,67,70,73,76,
    79,82,85,88,90,93,97,100,103,106,109,112,115,118,121,124
};

// Compact 6x8 bitmap font
static const uint8_t font_6x8[][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 Space
    {0x20,0x20,0x20,0x20,0x20,0x00,0x20,0x00}, // 33 !
    {0x50,0x50,0x50,0x00,0x00,0x00,0x00,0x00}, // 34 "
    {0x50,0x50,0xF8,0x50,0xF8,0x50,0x50,0x00}, // 35 #
    {0x20,0x78,0xA0,0x70,0x28,0xF0,0x20,0x00}, // 36 $
    {0xC0,0xC8,0x10,0x20,0x40,0x98,0x18,0x00}, // 37 %
    {0x40,0xA0,0xA0,0x40,0xA8,0x90,0x68,0x00}, // 38 &
    {0x20,0x20,0x40,0x00,0x00,0x00,0x00,0x00}, // 39 '
    {0x10,0x20,0x40,0x40,0x40,0x20,0x10,0x00}, // 40 (
    {0x40,0x20,0x10,0x10,0x10,0x20,0x40,0x00}, // 41 )
    {0x00,0x20,0xA8,0x70,0xA8,0x20,0x00,0x00}, // 42 *
    {0x00,0x20,0x20,0xF8,0x20,0x20,0x00,0x00}, // 43 +
    {0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x40}, // 44 ,
    {0x00,0x00,0x00,0xF8,0x00,0x00,0x00,0x00}, // 45 -
    {0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00}, // 46 .
    {0x00,0x08,0x10,0x20,0x40,0x80,0x00,0x00}, // 47 /
    {0x70,0x88,0x98,0xA8,0xC8,0x88,0x70,0x00}, // 48 0
    {0x20,0x60,0x20,0x20,0x20,0x20,0x70,0x00}, // 49 1
    {0x70,0x88,0x08,0x30,0x40,0x80,0xF8,0x00}, // 50 2
    {0x70,0x88,0x08,0x30,0x08,0x88,0x70,0x00}, // 51 3
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x10,0x00}, // 52 4
    {0xF8,0x80,0xF0,0x08,0x08,0x88,0x70,0x00}, // 53 5
    {0x30,0x40,0x80,0xF0,0x88,0x88,0x70,0x00}, // 54 6
    {0xF8,0x08,0x10,0x20,0x40,0x40,0x40,0x00}, // 55 7
    {0x70,0x88,0x88,0x70,0x88,0x88,0x70,0x00}, // 56 8
    {0x70,0x88,0x88,0x78,0x08,0x10,0x60,0x00}, // 57 9
    {0x00,0x00,0x20,0x00,0x00,0x20,0x00,0x00}, // 58 :
    {0x00,0x00,0x20,0x00,0x00,0x20,0x20,0x40}, // 59 ;
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00}, // 60 <
    {0x00,0x00,0xF8,0x00,0xF8,0x00,0x00,0x00}, // 61 =
    {0x40,0x20,0x10,0x08,0x10,0x20,0x40,0x00}, // 62 >
    {0x70,0x88,0x10,0x20,0x20,0x00,0x20,0x00}, // 63 ?
    {0x70,0x88,0xB8,0xA8,0xB8,0x80,0x70,0x00}, // 64 @
    {0x70,0x88,0x88,0xF8,0x88,0x88,0x88,0x00}, // 65 A
    {0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0,0x00}, // 66 B
    {0x70,0x88,0x80,0x80,0x80,0x88,0x70,0x00}, // 67 C
    {0xE0,0x90,0x88,0x88,0x88,0x90,0xE0,0x00}, // 68 D
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8,0x00}, // 69 E
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0x80,0x00}, // 70 F
    {0x70,0x88,0x80,0xB8,0x88,0x88,0x70,0x00}, // 71 G
    {0x88,0x88,0x88,0xF8,0x88,0x88,0x88,0x00}, // 72 H
    {0x70,0x20,0x20,0x20,0x20,0x20,0x70,0x00}, // 73 I
    {0x38,0x10,0x10,0x10,0x90,0x90,0x60,0x00}, // 74 J
    {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88,0x00}, // 75 K
    {0x80,0x80,0x80,0x80,0x80,0x80,0xF8,0x00}, // 76 L
    {0x88,0xD8,0xA8,0xA8,0x88,0x88,0x88,0x00}, // 77 M
    {0x88,0xC8,0xA8,0x98,0x88,0x88,0x88,0x00}, // 78 N
    {0x70,0x88,0x88,0x88,0x88,0x88,0x70,0x00}, // 79 O
    {0xF0,0x88,0x88,0xF0,0x80,0x80,0x80,0x00}, // 80 P
    {0x70,0x88,0x88,0x88,0xA8,0x90,0x68,0x00}, // 81 Q
    {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88,0x00}, // 82 R
    {0x70,0x88,0x80,0x70,0x08,0x88,0x70,0x00}, // 83 S
    {0xF8,0x20,0x20,0x20,0x20,0x20,0x20,0x00}, // 84 T
    {0x88,0x88,0x88,0x88,0x88,0x88,0x70,0x00}, // 85 U
    {0x88,0x88,0x88,0x88,0x50,0x50,0x20,0x00}, // 86 V
    {0x88,0x88,0x88,0xA8,0xA8,0xD8,0x88,0x00}, // 87 W
    {0x88,0x88,0x50,0x20,0x50,0x88,0x88,0x00}, // 88 X
    {0x88,0x88,0x50,0x20,0x20,0x20,0x20,0x00}, // 89 Y
    {0xF8,0x08,0x10,0x20,0x40,0x80,0xF8,0x00}, // 90 Z
    {0x70,0x40,0x40,0x40,0x40,0x40,0x70,0x00}, // 91 [
    {0x00,0x80,0x40,0x20,0x10,0x08,0x00,0x00}, // 92 backslash
    {0x70,0x10,0x10,0x10,0x10,0x10,0x70,0x00}, // 93 ]
    {0x20,0x50,0x88,0x00,0x00,0x00,0x00,0x00}, // 94 ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF8}, // 95 _
    {0x40,0x20,0x10,0x00,0x00,0x00,0x00,0x00}, // 96 `
    {0x00,0x00,0x70,0x08,0x78,0x88,0x78,0x00}, // 97 a
    {0x80,0x80,0xB0,0xC8,0x88,0xC8,0xB0,0x00}, // 98 b
    {0x00,0x00,0x70,0x80,0x80,0x88,0x70,0x00}, // 99 c
    {0x08,0x08,0x68,0x98,0x88,0x98,0x68,0x00}, // 100 d
    {0x00,0x00,0x70,0x88,0xF8,0x80,0x70,0x00}, // 101 e
    {0x30,0x48,0x40,0xE0,0x40,0x40,0x40,0x00}, // 102 f
    {0x00,0x00,0x68,0x98,0x98,0x68,0x08,0x70}, // 103 g
    {0x80,0x80,0xB0,0xC8,0x88,0x88,0x88,0x00}, // 104 h
    {0x20,0x00,0x60,0x20,0x20,0x20,0x70,0x00}, // 105 i
    {0x10,0x00,0x30,0x10,0x10,0x90,0x60,0x00}, // 106 j
    {0x80,0x80,0x90,0xA0,0xC0,0xA0,0x90,0x00}, // 107 k
    {0x60,0x20,0x20,0x20,0x20,0x20,0x70,0x00}, // 108 l
    {0x00,0x00,0xD0,0xA8,0xA8,0xA8,0xA8,0x00}, // 109 m
    {0x00,0x00,0xB0,0xC8,0x88,0x88,0x88,0x00}, // 110 n
    {0x00,0x00,0x70,0x88,0x88,0x88,0x70,0x00}, // 111 o
    {0x00,0x00,0xB0,0xC8,0xC8,0xB0,0x80,0x80}, // 112 p
    {0x00,0x00,0x68,0x98,0x98,0x68,0x08,0x08}, // 113 q
    {0x00,0x00,0xB0,0xC8,0x80,0x80,0x80,0x00}, // 114 r
    {0x00,0x00,0x78,0x80,0x70,0x08,0xF0,0x00}, // 115 s
    {0x40,0x40,0xE0,0x40,0x40,0x48,0x30,0x00}, // 116 t
    {0x00,0x00,0x88,0x88,0x88,0x98,0x68,0x00}, // 117 u
    {0x00,0x00,0x88,0x88,0x88,0x50,0x20,0x00}, // 118 v
    {0x00,0x00,0x88,0xA8,0xA8,0xA8,0x50,0x00}, // 119 w
    {0x00,0x00,0x88,0x50,0x20,0x50,0x88,0x00}, // 120 x
    {0x00,0x00,0x88,0x88,0x98,0x68,0x08,0x70}, // 121 y
    {0x00,0x00,0xF8,0x10,0x20,0x40,0xF8,0x00}, // 122 z
    {0x10,0x20,0x20,0x40,0x20,0x20,0x10,0x00}, // 123 {
    {0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x00}, // 124 |
    {0x40,0x20,0x20,0x10,0x20,0x20,0x40,0x00}, // 125 }
    {0x00,0x00,0x40,0xA8,0x10,0x00,0x00,0x00}, // 126 ~
};

// Helper: interpolate between two colors
static uint32_t lerp_color(uint32_t c1, uint32_t c2, uint8_t t) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t / 255);
    uint8_t g = g1 + ((g2 - g1) * t / 255);
    uint8_t b = b1 + ((b2 - b1) * t / 255);
    return (r << 16) | (g << 8) | b;
}

// Set up demoscene palette with smooth gradients
static void setup_demoscene_palette(void) {
    // Gradient keypoints for plasma effect (dark blue -> cyan -> white -> magenta -> dark purple)
    static const uint32_t gradient[] = {
        0x000020,  // Very dark blue
        0x000060,  // Dark blue
        0x0000C0,  // Blue
        0x0040FF,  // Bright blue
        0x00C0FF,  // Cyan-blue
        0x00FFFF,  // Cyan
        0x80FFFF,  // Light cyan
        0xFFFFFF,  // White
        0xFF80FF,  // Light magenta
        0xFF00FF,  // Magenta
        0xC000C0,  // Purple
        0x600080,  // Dark purple
        0x200040,  // Very dark purple
        0x000020,  // Back to dark blue (for seamless loop)
    };
    const int num_keys = sizeof(gradient) / sizeof(gradient[0]);

    // Generate smooth gradient across plasma palette range
    for (int i = 0; i < PALETTE_PLASMA_COUNT; i++) {
        float pos = (float)i / PALETTE_PLASMA_COUNT * (num_keys - 1);
        int idx = (int)pos;
        uint8_t t = (uint8_t)((pos - idx) * 255);
        if (idx >= num_keys - 1) idx = num_keys - 2;
        uint32_t color = lerp_color(gradient[idx], gradient[idx + 1], t);
        graphics_set_palette(PALETTE_PLASMA_START + i, color);
    }

    // Copper bar colors: warm gradient (dark red -> orange -> yellow -> white)
    static const uint32_t copper_gradient[] = {
        0x200000,  // Very dark red
        0x600000,  // Dark red
        0xC00000,  // Red
        0xFF2000,  // Orange-red
        0xFF6000,  // Orange
        0xFFA000,  // Light orange
        0xFFC040,  // Yellow-orange
        0xFFE080,  // Light yellow
        0xFFF0C0,  // Pale yellow
        0xFFFFFF,  // White (center)
        0xFFF0C0,  // Pale yellow
        0xFFE080,  // Light yellow
        0xFFC040,  // Yellow-orange
        0xFFA000,  // Light orange
        0xFF6000,  // Orange
        0xC00000,  // Red
    };
    for (int i = 0; i < PALETTE_COPPER_COUNT; i++) {
        graphics_set_palette(PALETTE_COPPER_START + i, copper_gradient[i]);
    }

    // Special colors for text (indices 250-255)
    graphics_set_palette(COLOR_TEXT_SHADOW, 0x101030);  // 250
    graphics_set_palette(COLOR_TEXT_CYAN, 0x00FFFF);    // 251
    graphics_set_palette(COLOR_TEXT_YELLOW, 0xFFFF00);  // 252
    graphics_set_palette(COLOR_TEXT_GREEN, 0x00FF00);   // 253
    graphics_set_palette(254, 0xC0C0C0);                // Light gray
    graphics_set_palette(COLOR_TEXT_WHITE, 0xFFFFFF);   // 255
}

// Draw plasma background
static void draw_plasma(uint8_t *fb, uint8_t time_offset) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            // Smooth plasma formula using multiple overlapping sine waves
            // Scale x and y to create seamless tiling pattern
            uint8_t v1 = sine_table[(x * 2 + time_offset) & 0xFF];
            uint8_t v2 = sine_table[(y * 2 + time_offset) & 0xFF];
            uint8_t v3 = sine_table[((x + y) + time_offset * 2) & 0xFF];
            uint8_t v4 = sine_table[((x - y + 256) + time_offset) & 0xFF];

            // Add a slow-moving radial component for more variation
            int cx = x - SCREEN_WIDTH / 2;
            int cy = y - SCREEN_HEIGHT / 2;
            uint8_t v5 = sine_table[((cx * cx + cy * cy) / 128 + time_offset * 3) & 0xFF];

            // Combine waves with equal weight
            uint16_t combined = v1 + v2 + v3 + v4 + v5;

            // Map to palette range with clamping
            int plasma_idx = (combined * PALETTE_PLASMA_COUNT) / (256 * 5);
            if (plasma_idx >= PALETTE_PLASMA_COUNT) plasma_idx = PALETTE_PLASMA_COUNT - 1;

            fb[y * SCREEN_WIDTH + x] = PALETTE_PLASMA_START + plasma_idx;
        }
    }
}

// Draw horizontal copper bars (classic Amiga effect)
static void draw_copper_bars(uint8_t *fb, int bar_y, int bar_height) {
    if (bar_y < 0 || bar_y >= SCREEN_HEIGHT) return;

    for (int i = 0; i < bar_height && (bar_y + i) < SCREEN_HEIGHT; i++) {
        int y = bar_y + i;
        // Calculate color index based on position in bar (peak at center)
        int dist_from_center = (i < bar_height / 2) ? i : (bar_height - 1 - i);
        int color_idx = dist_from_center * (PALETTE_COPPER_COUNT - 1) / (bar_height / 2);
        if (color_idx >= PALETTE_COPPER_COUNT) color_idx = PALETTE_COPPER_COUNT - 1;

        uint8_t color = PALETTE_COPPER_START + color_idx;

        // Draw full width scanline
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            fb[y * SCREEN_WIDTH + x] = color;
        }
    }
}

// Draw a character with drop shadow
static void draw_char_shadow(uint8_t *fb, int x, int y, char c, uint8_t color) {
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx > 94) return;

    const uint8_t *glyph = font_6x8[idx];

    // Draw shadow first (offset by 1,1)
    for (int row = 0; row < 8; row++) {
        if (y + row + 1 < 0 || y + row + 1 >= SCREEN_HEIGHT) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 6; col++) {
            if (x + col + 1 < 0 || x + col + 1 >= SCREEN_WIDTH) continue;
            if (bits & (0x80 >> col)) {
                fb[(y + row + 1) * SCREEN_WIDTH + (x + col + 1)] = COLOR_TEXT_SHADOW;
            }
        }
    }

    // Draw main character
    for (int row = 0; row < 8; row++) {
        if (y + row < 0 || y + row >= SCREEN_HEIGHT) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 6; col++) {
            if (x + col < 0 || x + col >= SCREEN_WIDTH) continue;
            if (bits & (0x80 >> col)) {
                fb[(y + row) * SCREEN_WIDTH + (x + col)] = color;
            }
        }
    }
}

// Draw string with drop shadow
static void draw_string_shadow(uint8_t *fb, int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char_shadow(fb, x, y, *str, color);
        x += CHAR_WIDTH;
        str++;
    }
}

// Get string width in pixels
static int string_width(const char *str) {
    return strlen(str) * CHAR_WIDTH;
}

// Draw centered string with shadow
static void draw_centered_shadow(uint8_t *fb, int y, const char *str, uint8_t color) {
    int w = string_width(str);
    int x = (SCREEN_WIDTH - w) / 2;
    draw_string_shadow(fb, x, y, str, color);
}

// Draw a semi-transparent box (darkens background)
static void draw_dark_box(uint8_t *fb, int x, int y, int w, int h) {
    for (int dy = 0; dy < h; dy++) {
        if (y + dy < 0 || y + dy >= SCREEN_HEIGHT) continue;
        for (int dx = 0; dx < w; dx++) {
            if (x + dx < 0 || x + dx >= SCREEN_WIDTH) continue;
            int idx = (y + dy) * SCREEN_WIDTH + (x + dx);
            // Darken by reducing to lower palette entries
            uint8_t current = fb[idx];
            if (current >= PALETTE_PLASMA_START) {
                // Shift toward darker end of plasma palette
                int new_val = PALETTE_PLASMA_START + (current - PALETTE_PLASMA_START) / 3;
                fb[idx] = new_val;
            }
        }
    }
}

// Draw a glowing border around a box
static void draw_glow_border(uint8_t *fb, int x, int y, int w, int h, uint8_t base_color) {
    // Draw outer glow (darker)
    for (int i = -2; i <= w + 1; i++) {
        if (x + i >= 0 && x + i < SCREEN_WIDTH) {
            if (y - 2 >= 0) fb[(y - 2) * SCREEN_WIDTH + x + i] = base_color;
            if (y + h + 1 < SCREEN_HEIGHT) fb[(y + h + 1) * SCREEN_WIDTH + x + i] = base_color;
        }
    }
    for (int i = -1; i <= h; i++) {
        if (y + i >= 0 && y + i < SCREEN_HEIGHT) {
            if (x - 2 >= 0) fb[(y + i) * SCREEN_WIDTH + x - 2] = base_color;
            if (x + w + 1 < SCREEN_WIDTH) fb[(y + i) * SCREEN_WIDTH + x + w + 1] = base_color;
        }
    }

    // Draw bright inner border
    uint8_t bright = base_color + 40;
    if (bright > PALETTE_PLASMA_START + PALETTE_PLASMA_COUNT - 1)
        bright = PALETTE_PLASMA_START + PALETTE_PLASMA_COUNT - 1;

    for (int i = -1; i <= w; i++) {
        if (x + i >= 0 && x + i < SCREEN_WIDTH) {
            if (y - 1 >= 0) fb[(y - 1) * SCREEN_WIDTH + x + i] = bright;
            if (y + h < SCREEN_HEIGHT) fb[(y + h) * SCREEN_WIDTH + x + i] = bright;
        }
    }
    for (int i = 0; i < h; i++) {
        if (y + i >= 0 && y + i < SCREEN_HEIGHT) {
            if (x - 1 >= 0) fb[(y + i) * SCREEN_WIDTH + x - 1] = bright;
            if (x + w < SCREEN_WIDTH) fb[(y + i) * SCREEN_WIDTH + x + w] = bright;
        }
    }
}

int startscreen_show(startscreen_info_t *info) {
#if 0
    // Setup demoscene color palette
    setup_demoscene_palette();

    // Animate the plasma for visual effect
    const int num_frames = 120;  // ~4 seconds at 30fps
    const int frame_delay = 33;  // ~30fps for smooth animation

    int back_buffer_idx = 1;  // Start drawing to buffer 1

    for (int frame = 0; frame < num_frames; frame++) {
        // Get back buffer to draw to
        uint8_t *buffer = framebuffers[back_buffer_idx];

        // Draw animated plasma background
        draw_plasma(buffer, frame * 4);

        // Add copper bars at different positions
        int bar1_y = 20 + (sine_table[(frame * 3) & 0xFF] * 30 / 255);
        int bar2_y = SCREEN_HEIGHT - 50 - (sine_table[(frame * 3 + 128) & 0xFF] * 30 / 255);
        draw_copper_bars(buffer, bar1_y, 12);
        draw_copper_bars(buffer, bar2_y, 12);

        // Info box dimensions
        const int box_w = 240;
        const int box_h = 140;
        const int box_x = (SCREEN_WIDTH - box_w) / 2;
        const int box_y = (SCREEN_HEIGHT - box_h) / 2;

        // Draw darkened info box area
        draw_dark_box(buffer, box_x, box_y, box_w, box_h);

        // Draw glowing border
        draw_glow_border(buffer, box_x, box_y, box_w, box_h, PALETTE_PLASMA_START + 100);

        // Text content
        int text_y = box_y + 12;

        // Title (large, centered)
        draw_centered_shadow(buffer, text_y, info->title, COLOR_TEXT_WHITE);
        text_y += LINE_HEIGHT + 4;

        // Subtitle
        draw_centered_shadow(buffer, text_y, info->subtitle, COLOR_TEXT_CYAN);
        text_y += LINE_HEIGHT + 2;

        // Version (green)
        draw_centered_shadow(buffer, text_y, info->version, COLOR_TEXT_GREEN);
        text_y += LINE_HEIGHT + 12;

        // System info
        char line[48];

        snprintf(line, sizeof(line), "CPU: %lu MHz", (unsigned long)info->cpu_mhz);
        draw_centered_shadow(buffer, text_y, line, COLOR_TEXT_WHITE);
        text_y += LINE_HEIGHT + 2;

#ifdef PSRAM_MAX_FREQ_MHZ
        snprintf(line, sizeof(line), "PSRAM: %lu MHz", (unsigned long)info->psram_mhz);
        draw_centered_shadow(line, text_y, line, COLOR_TEXT_WHITE);
        text_y += LINE_HEIGHT + 2;
#endif

        snprintf(line, sizeof(line), "Board: %s", info->board_variant);
        draw_centered_shadow(buffer, text_y, line, COLOR_TEXT_WHITE);
        text_y += LINE_HEIGHT + 10;

        // Credits
        draw_centered_shadow(buffer, text_y, "By Mikhail Matveev", COLOR_TEXT_CYAN);
        text_y += LINE_HEIGHT;
        draw_centered_shadow(buffer, text_y, "rh1.tech", COLOR_TEXT_CYAN);

        // Starting message at bottom (blinking on later frames, green)
        if (frame > 60 || (frame / 8) % 2 == 0) {
            text_y = box_y + box_h - 16;
            draw_centered_shadow(buffer, text_y, "Starting...", COLOR_TEXT_GREEN);
        }

        // Swap buffers - display what we just drew
        graphics_request_buffer_swap(buffer);

        // Toggle buffer index for next frame
        back_buffer_idx = 1 - back_buffer_idx;

        // Wait for next frame
        sleep_ms(frame_delay);
    }

    // Hold final frame briefly
    sleep_ms(500);
#endif
    return 0;
}
