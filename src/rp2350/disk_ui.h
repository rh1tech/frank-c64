/*
 * disk_ui.h
 *
 * Simple text-based disk selector UI for MurmC64
 * Adapted from murmc64/murmapple
 *
 * Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 */

#ifndef DISK_UI_H
#define DISK_UI_H

#include <stdint.h>
#include <stdbool.h>

// UI state
typedef enum {
    DISK_UI_HIDDEN,
    DISK_UI_SELECT_FILE,    // Selecting disk image file
    DISK_UI_SELECT_ACTION,  // Selecting action: Load, Mount, or Cancel
    DISK_UI_LOADING,        // Loading disk from SD card
} disk_ui_state_t;

// Initialize disk UI
void disk_ui_init(void);

// Show the disk selector (called on F11)
void disk_ui_show(void);

// Hide the disk selector (called on Esc)
void disk_ui_hide(void);

// Toggle visibility
void disk_ui_toggle(void);

// Handle key press in disk UI
// Returns true if key was consumed
bool disk_ui_handle_key(uint8_t key);

// Render the disk UI overlay to framebuffer
void disk_ui_render(uint8_t *framebuffer);

// Check if UI is visible
bool disk_ui_is_visible(void);

// Move selection up
void disk_ui_move_up(void);

// Move selection down
void disk_ui_move_down(void);

void disk_ui_page_up(void);
void disk_ui_page_down(void);
void disk_ui_home(void);
void disk_ui_end(void);

// Get selected index
int disk_ui_get_selected(void);

// Select current file and show action menu
void disk_ui_select(void);

// Get current UI state
int disk_ui_get_state(void);

// Action selection
void disk_ui_action_up(void);
void disk_ui_action_down(void);
int disk_ui_get_action(void);  // 0=Load, 1=Mount
void disk_ui_confirm_action(void);
void disk_ui_cancel_action(void);

#endif // DISK_UI_H
