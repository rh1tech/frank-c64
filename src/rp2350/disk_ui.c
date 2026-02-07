/*
 * disk_ui.c
 *
 * Disk selector UI for MurmC64
 * Adapted from murmc64/murmapple
 * Features inverted title bar, compact 6x8 font, proper selection highlighting
 *
 * Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 */

#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "debug_log.h"
#include "disk_ui.h"

// Forward declarations for disk loader (TODO: -> .h)
#define MAX_FILENAME_LEN    64
typedef struct {
    char name[MAX_FILENAME_LEN];
    uint32_t size;
    uint8_t type;  // 0=D64, 1=G64, 2=T64, 3=TAP, 4=PRG, 5=CRT, 6=D81, 7=DIR
} disk_entry_t;

extern int disk_loader_get_count(void);
extern const char *disk_loader_get_filename(int index);
extern const char *disk_loader_get_path(int index);
extern const disk_entry_t *disk_loader_get_entry(int index);
extern int disk_loader_scan_dir(const char *path);
extern const char *disk_loader_get_cwd(void);
extern int disk_loader_delete(int index);

// UI state
static volatile disk_ui_state_t ui_state = DISK_UI_HIDDEN;
static volatile int selected_file = 0;
static volatile int selected_action = 0;
static volatile int scroll_offset = 0;

// UI dimensions - designed for 320x240 display
#define UI_X            24
#define UI_Y            20
#define UI_WIDTH        272
#define UI_HEIGHT       200
#define UI_PADDING      6
#define CHAR_WIDTH      6
#define CHAR_HEIGHT     8
#define HEADER_HEIGHT   12
#define LINE_HEIGHT     10
#define MAX_VISIBLE     16
#define MAX_DISPLAY_LEN 40

// Colors (C64 palette indices)
#define COLOR_BG        0   // Black
#define COLOR_BORDER    14  // Light Blue
#define COLOR_TEXT      1   // White
#define COLOR_HEADER_BG 14  // Light Blue
#define COLOR_HEADER_FG 0   // Black
#define COLOR_SELECT_BG 14  // Light Blue
#define COLOR_SELECT_FG 0   // Black

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

extern char current_scan_path[128];

static inline bool has_parent_dir(void)
{
    return current_scan_path[1] != 0; // "/" - root
}

static void clamp_scroll(void)
{
    if (selected_file < scroll_offset)
        scroll_offset = selected_file;

    if (selected_file >= scroll_offset + MAX_VISIBLE)
        scroll_offset = selected_file - MAX_VISIBLE + 1;

    if (scroll_offset < 0)
        scroll_offset = 0;
}

// Draw a filled rectangle
static void draw_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color) {
    for (int dy = 0; dy < h; dy++) {
        if (y + dy < 0 || y + dy >= FB_HEIGHT) continue;
        for (int dx = 0; dx < w; dx++) {
            if (x + dx < 0 || x + dx >= FB_WIDTH) continue;
            fb[(y + dy) * FB_WIDTH + (x + dx)] = color;
        }
    }
}

// Draw a character using the 6x8 bitmap font
static void draw_char(uint8_t *fb, int x, int y, char c, uint8_t color) {
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx > 94) return;

    const uint8_t *glyph = font_6x8[idx];

    for (int row = 0; row < 8; row++) {
        if (y + row < 0 || y + row >= FB_HEIGHT) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 6; col++) {
            if (x + col < 0 || x + col >= FB_WIDTH) continue;
            if (bits & (0x80 >> col)) {
                fb[(y + row) * FB_WIDTH + (x + col)] = color;
            }
        }
    }
}

// Draw a string
static void draw_string(uint8_t *fb, int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char(fb, x, y, *str, color);
        x += CHAR_WIDTH;
        str++;
    }
}

// Draw a string with truncation
static void draw_string_truncated(uint8_t *fb, int x, int y, const char *str, int max_chars, uint8_t color) {
    int len = strlen(str);
    if (len <= max_chars) {
        draw_string(fb, x, y, str, color);
    } else {
        for (int i = 0; i < max_chars - 3; i++) {
            draw_char(fb, x + i * CHAR_WIDTH, y, str[i], color);
        }
        draw_string(fb, x + (max_chars - 3) * CHAR_WIDTH, y, "...", color);
    }
}

// Draw header bar
static void draw_header(uint8_t *fb, int x, int y, int w, const char *title) {
    draw_rect(fb, x, y, w, HEADER_HEIGHT, COLOR_HEADER_BG);
    int title_len = strlen(title);
    int title_x = x + (w - title_len * CHAR_WIDTH) / 2;
    int title_y = y + (HEADER_HEIGHT - CHAR_HEIGHT) / 2;
    draw_string(fb, title_x, title_y, title, COLOR_HEADER_FG);
}

// Draw a menu item
static void draw_menu_item(uint8_t *fb, int x, int y, int w, const char *text, int max_chars, bool selected) {
    if (selected) {
        draw_rect(fb, x, y, w, LINE_HEIGHT, COLOR_SELECT_BG);
        draw_string_truncated(fb, x + 2, y + 1, text, max_chars, COLOR_SELECT_FG);
    } else {
        draw_rect(fb, x, y, w, LINE_HEIGHT, COLOR_BG);
        draw_string_truncated(fb, x + 2, y + 1, text, max_chars, COLOR_TEXT);
    }
}

// Draw border
static void draw_border(uint8_t *fb, int x, int y, int w, int h) {
    draw_rect(fb, x, y, w, 1, COLOR_BORDER);
    draw_rect(fb, x, y + h - 1, w, 1, COLOR_BORDER);
    draw_rect(fb, x, y, 1, h, COLOR_BORDER);
    draw_rect(fb, x + w - 1, y, 1, h, COLOR_BORDER);
}

void disk_ui_init(void) {
    ui_state = DISK_UI_HIDDEN;
    selected_file = 0;
    selected_action = 0;
    scroll_offset = 0;
}

void disk_ui_show(void) {
    if (ui_state == DISK_UI_HIDDEN) {
        disk_loader_scan_dir(current_scan_path);
        ui_state = DISK_UI_SELECT_FILE;
        scroll_offset = 0;
        clamp_scroll();
        MII_DEBUG_PRINTF("Disk UI: showing file selection\n");
    }
}

void disk_ui_hide(void) {
    ui_state = DISK_UI_HIDDEN;
    MII_DEBUG_PRINTF("Disk UI: hidden\n");
}

void disk_ui_toggle(void) {
    if (ui_state == DISK_UI_HIDDEN) {
        disk_ui_show();
    } else {
        disk_ui_hide();
    }
}

bool disk_ui_is_visible(void) {
    return ui_state != DISK_UI_HIDDEN;
}

int disk_ui_get_count(void) {
    return disk_loader_get_count() + (has_parent_dir() ? 1 : 0);
}

void disk_ui_move_up(void) {
    int count = disk_ui_get_count();
    if (count == 0) return;

    if (selected_file > 0) {
        selected_file--;
    } else {
        selected_file = count - 1;
        scroll_offset = (count > MAX_VISIBLE) ? count - MAX_VISIBLE : 0;
    }
    if (selected_file < scroll_offset) {
        scroll_offset = selected_file;
    }
}

void disk_ui_move_down(void) {
    int count = disk_ui_get_count();
    if (count == 0) return;

    if (selected_file < count - 1) {
        selected_file++;
    } else {
        selected_file = 0;
        scroll_offset = 0;
    }
    if (selected_file >= scroll_offset + MAX_VISIBLE) {
        scroll_offset = selected_file - MAX_VISIBLE + 1;
    }
}

void disk_ui_page_up(void)
{
    int count = disk_ui_get_count();
    if (count == 0) return;

    selected_file -= MAX_VISIBLE / 2;
    if (selected_file < 0)
        selected_file = 0;

    // если курсор ушёл выше окна — подтянуть окно
    if (selected_file < scroll_offset)
        scroll_offset = selected_file;
}

void disk_ui_page_down(void)
{
    int count = disk_ui_get_count();
    if (count == 0) return;

    selected_file += MAX_VISIBLE / 2;
    if (selected_file >= count)
        selected_file = count - 1;

    // если курсор ушёл ниже окна — сдвинуть окно
    if (selected_file >= scroll_offset + MAX_VISIBLE)
        scroll_offset = selected_file - MAX_VISIBLE + 1;
}

void disk_ui_home(void)
{
    int count = disk_ui_get_count();
    if (count == 0) return;

    selected_file = 0;

    // если окно было ниже — подтянуть вверх
    if (scroll_offset != 0)
        scroll_offset = 0;
}

void disk_ui_end(void)
{
    int count = disk_ui_get_count();
    if (count == 0) return;

    selected_file = count - 1;

    // если курсор ниже окна — прокрутить так,
    // чтобы последний элемент был виден
    int min_scroll = count - MAX_VISIBLE;
    if (min_scroll < 0)
        min_scroll = 0;

    if (scroll_offset != min_scroll)
        scroll_offset = min_scroll;
}

int disk_ui_get_selected(void) {
    if (has_parent_dir()) {
        return selected_file - 1;
    }
    return selected_file;
}

void disk_ui_select(void) {
    if (ui_state == DISK_UI_SELECT_FILE && disk_ui_get_count() > 0) {
        int base = has_parent_dir() ? 1 : 0;

        // ".."
        if (has_parent_dir() && selected_file == 0) {
            char *p = strrchr(current_scan_path, '/');
            if (p && p != current_scan_path)
                *p = 0;
            else
                strcpy(current_scan_path, "/");

            disk_loader_scan_dir(current_scan_path);
            selected_file = 0;
            scroll_offset = 0;
            return;
        }

        int real = selected_file - base;
        const disk_entry_t *e = disk_loader_get_entry(real);
        if (!e)
            return;

        // directory enter
        if (e->type == 7) {
            char new_path[128];
            snprintf(new_path, sizeof(new_path), "%s/%s",
                    current_scan_path, e->name);
            strncpy(current_scan_path, new_path, sizeof(current_scan_path)-1);
            disk_loader_scan_dir(current_scan_path);
            selected_file = 0;
            scroll_offset = 0;
            return;
        }

        // file → action dialog
        ui_state = DISK_UI_SELECT_ACTION;
        selected_action = 0;  // Default to "Load"
        MII_DEBUG_PRINTF("Disk UI: showing action selection for file %d\n", selected_file);
    }
}

int disk_ui_get_state(void) {
    return (int)ui_state;
}

void disk_ui_action_up(void) {
    if (ui_state == DISK_UI_SELECT_ACTION) {
        selected_action = (selected_action > 0) ? selected_action - 1 : 1;
    }
}

void disk_ui_action_down(void) {
    if (ui_state == DISK_UI_SELECT_ACTION) {
        selected_action = (selected_action < 1) ? selected_action + 1 : 0;
    }
}

int disk_ui_get_action(void) {
    return selected_action;
}

void disk_ui_confirm_action(void) {
    // Called after action is confirmed - just hide the UI
    // The actual loading/mounting is done by the caller
    disk_ui_hide();
}

void disk_ui_cancel_action(void) {
    if (ui_state == DISK_UI_SELECT_ACTION) {
        ui_state = DISK_UI_SELECT_FILE;
        MII_DEBUG_PRINTF("Disk UI: cancelled action, back to file selection\n");
    }
}

void disk_ui_delete(void) {
    if (has_parent_dir() && selected_file == 0)
        return;

    int base = has_parent_dir() ? 1 : 0;
    int idx = selected_file - base;
    if (disk_loader_delete(idx) == 0) {
        disk_loader_scan_dir(current_scan_path);
        int count = disk_ui_get_count();
        if (selected_file >= count)
            selected_file = count ? count - 1 : 0;
    }
}

bool disk_ui_handle_key(uint8_t key) {
    if (ui_state == DISK_UI_HIDDEN) {
        return false;
    }
    switch (key) {
        case 0x1B:  // Escape
            disk_ui_hide();
            return true;
        case 0x0B:  // Up
        case 0x08:  // Backspace
            disk_ui_move_up();
            return true;
        case 0x0A:  // Down
        case 0x15:  // Right
            disk_ui_move_down();
            return true;
    }
    return false;
}

// Draw action selection dialog (small popup over file list)
static void draw_action_dialog(uint8_t *framebuffer) {
    // Action dialog dimensions - smaller centered box
    int dlg_width = 160;
    int dlg_height = 70;
    int dlg_x = UI_X + (UI_WIDTH - dlg_width) / 2;
    int dlg_y = UI_Y + (UI_HEIGHT - dlg_height) / 2;

    // Draw dialog background
    draw_rect(framebuffer, dlg_x, dlg_y, dlg_width, dlg_height, COLOR_BG);

    // Draw border
    draw_border(framebuffer, dlg_x, dlg_y, dlg_width, dlg_height);

    // Draw header
    draw_header(framebuffer, dlg_x, dlg_y, dlg_width, " Action ");

    int content_x = dlg_x + UI_PADDING;
    int content_y = dlg_y + HEADER_HEIGHT + UI_PADDING;
    int item_width = dlg_width - UI_PADDING * 2;

    // Draw "Load" option
    draw_menu_item(framebuffer, content_x, content_y, item_width,
                   "Load (Run)", 20, selected_action == 0);

    // Draw "Mount" option
    draw_menu_item(framebuffer, content_x, content_y + LINE_HEIGHT + 2, item_width,
                   "Mount (Insert)", 20, selected_action == 1);

    // Draw footer
    int footer_y = dlg_y + dlg_height - LINE_HEIGHT - 2;
    draw_string(framebuffer, content_x, footer_y, "[Enter] OK [Esc] Back", COLOR_TEXT);
}

void disk_ui_render(uint8_t *framebuffer) {
    if (ui_state == DISK_UI_HIDDEN || !framebuffer) {
        return;
    }

    int count = disk_ui_get_count();
    int content_x = UI_X + UI_PADDING;
    int content_y = UI_Y + HEADER_HEIGHT + UI_PADDING;
    int content_width = UI_WIDTH - UI_PADDING * 2;
    int max_chars = (content_width - 4) / CHAR_WIDTH;

    // Draw dialog background
    draw_rect(framebuffer, UI_X, UI_Y, UI_WIDTH, UI_HEIGHT, COLOR_BG);

    // Draw border
    draw_border(framebuffer, UI_X, UI_Y, UI_WIDTH, UI_HEIGHT);

    // Draw header
    draw_header(framebuffer, UI_X, UI_Y, UI_WIDTH, " Select Disk Image ");

    int y = content_y;

    if (count == 0) {
        draw_string(framebuffer, content_x, y, "No disk images found", COLOR_TEXT);
        draw_string(framebuffer, content_x, y + LINE_HEIGHT, "Place .d64/.g64/.prg in /c64", COLOR_TEXT);
    } else {
        int base = has_parent_dir() ? 1 : 0;
        int total = count;
        int visible = (total < MAX_VISIBLE) ? total : MAX_VISIBLE;

        for (int i = 0; i < visible; i++) {
            int ui_idx = scroll_offset + i;
            if (ui_idx >= total) break;

            bool selected = (ui_idx == selected_file);
            if (has_parent_dir() && ui_idx == 0) {
                draw_menu_item(framebuffer, content_x, y,
                               content_width - 8, "..",
                               max_chars - 2, selected);
            } else {
                int real = ui_idx - base;
                const disk_entry_t *e = disk_loader_get_entry(real);
                if (!e) continue;
                draw_menu_item(framebuffer, content_x, y,
                               content_width - 8, e->name,
                               max_chars - 2, selected);
            }
            y += LINE_HEIGHT;
        }
    }

    // Draw footer
    int footer_y = UI_Y + UI_HEIGHT - LINE_HEIGHT - 4;
    draw_string(framebuffer, content_x, footer_y, "[Up/Dn] Select [Enter] Load [F11] Cancel", COLOR_TEXT);

    // If in action selection mode, draw the action dialog on top
    if (ui_state == DISK_UI_SELECT_ACTION) {
        draw_action_dialog(framebuffer);
    }
}
