/*
 *  main_rp2350.c - Main entry point for MurmC64 (Frodo4 C64 Emulator) on RP2350
 *
 *  This initializes all hardware and starts the emulator.
 *
 *  Core 0: CPU emulation, VIC, SID, CIA, 1541
 *  Core 1: HDMI rendering
 *
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 */

#include "board_config.h"
#include "debug_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"

// Drivers
#include "HDMI.h"
#include "psram_init.h"
#include "psram_allocator.h"
#include "nespad/nespad.h"

#if ENABLE_PS2_KEYBOARD
#include "ps2kbd/ps2kbd_wrapper.h"
#endif

// USB HID support
#ifdef USB_HID_ENABLED
#include "usbhid/usbhid_wrapper.h"
#endif

// SD card and FAT filesystem
#include "sdcard/sdcard.h"
#include "fatfs/ff.h"

// Start screen
#include "startscreen.h"

// Forward declarations for timing functions
static uint32_t rp2350_get_ticks_ms(void);
static uint64_t rp2350_get_ticks_us(void);

// Forward declarations from C++ emulator
#ifdef __cplusplus
extern "C" {
#endif

// C64 emulator interface (from C64_rp2350.cpp)
void c64_init(void);
void c64_reset(void);
bool c64_run_frame(void);
uint8_t *c64_get_framebuffer(void);
void c64_set_drive_leds(int l0, int l1, int l2, int l3);
void c64_show_notification(const char *msg);

// Audio interface (from sid_i2s.cpp)
void sid_i2s_init(void);
void sid_i2s_update(void);

// Input interface (from input_rp2350.cpp)
void input_rp2350_init(void);
void input_rp2350_poll(uint8_t *key_matrix, uint8_t *rev_matrix, uint8_t *joystick);

// Disk loader (from disk_loader.c)
void disk_loader_init(void);
void disk_loader_scan(void);

#ifdef __cplusplus
}
#endif

// Global framebuffer pointers (used by Display_rp2350.cpp)
uint8_t *current_framebuffer = NULL;
volatile int current_fb_index = 0;
uint8_t *framebuffers[2] = { NULL, NULL };

//=============================================================================
// Global State
//=============================================================================

static volatile bool g_emulator_ready = false;
static volatile bool g_quit_requested = false;

// Double-buffered framebuffers (in SRAM for fast DMA access)
static uint8_t __attribute__((aligned(4))) g_framebuffer_a[FB_WIDTH * FB_HEIGHT];
static uint8_t __attribute__((aligned(4))) g_framebuffer_b[FB_WIDTH * FB_HEIGHT];
static uint8_t *g_front_buffer = g_framebuffer_a;
static uint8_t *g_back_buffer = g_framebuffer_b;

//=============================================================================
// Core 1: Video Rendering Task
//=============================================================================

#ifdef VIDEO_HDMI
static void core1_video_task(void) {
    MII_DEBUG_PRINTF("Core 1: Starting video task\n");
    multicore_lockout_victim_init();

    // Initialize HDMI IRQ handler on this core
    MII_DEBUG_PRINTF("Core 1: Calling graphics_init_irq_on_this_core()...\n");
    graphics_init_irq_on_this_core();
    MII_DEBUG_PRINTF("Core 1: IRQ initialized\n");

    uint32_t last_frame_count = 0;
    uint32_t debug_counter = 0;

    while (!g_quit_requested) {
        // Wait for emulator to be ready
        if (!g_emulator_ready) {
            sleep_ms(1);
            continue;
        }

        // Wait for vsync (new frame)
        uint32_t frame_count = get_frame_count();
        if (frame_count == last_frame_count) {
            // No new frame yet, yield
            tight_loop_contents();
            continue;
        }
        last_frame_count = frame_count;

        // Silent operation - debug disabled
        debug_counter++;

        // Check for buffer swap request
        // The front buffer is being displayed, back buffer is being rendered to
        // When emulator finishes a frame, it swaps buffers

        // Periodically check HDMI health
        #ifdef VIDEO_HDMI
        hdmi_check_and_restart();
        #endif
    }

    MII_DEBUG_PRINTF("Core 1: Video task ending\n");
}
#endif

//=============================================================================
// System Initialization
//=============================================================================

// Flash timing configuration for overclocking
#ifndef FLASH_MAX_FREQ_MHZ
#define FLASH_MAX_FREQ_MHZ 88
#endif

static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz) {
    const int clock_hz = cpu_mhz * 1000000;
    const int max_flash_freq = FLASH_MAX_FREQ_MHZ * 1000000;

    int divisor = (clock_hz + max_flash_freq - (max_flash_freq >> 4) - 1) / max_flash_freq;
    if (divisor == 1 && clock_hz >= 166000000) {
        divisor = 2;
    }

    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) {
        rxdelay += 1;
    }

    qmi_hw->m[0].timing = 0x60007000 |
                        rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                        divisor << QMI_M0_TIMING_CLKDIV_LSB;
}

static void __no_inline_not_in_flash_func(init_clocks)(void) {
    // Overclock BEFORE stdio_init_all() - matching murmgenesis approach
#if CPU_CLOCK_MHZ > 252
    // Disable voltage limit for high voltages (>1.50V)
    vreg_disable_voltage_limit();
    vreg_set_voltage(CPU_VOLTAGE);
    // Set flash timings before changing clock
    set_flash_timings(CPU_CLOCK_MHZ);
    sleep_ms(100);  // Let voltage stabilize (longer delay for high voltage)
#endif

    // Set system clock
    if (!set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, false)) {
        // Fallback to safe speed if requested speed fails
        set_sys_clock_khz(252 * 1000, true);
    }
}

static void __no_inline_not_in_flash_func(init_stdio)(void) {
#if ENABLE_DEBUG_LOGS
    // Initialize stdio AFTER clock is stable
    stdio_init_all();
    // Startup delay for USB serial console
    for (int i = 0; i < 8; i++) {
        sleep_ms(500);
    }
    // Print startup banner (no delay - USB Serial should already be enumerated)
    printf("\n\n");

    // Check reset reason from POWMAN_CHIP_RESET register (0x40100000 + 0x2C)
    volatile uint32_t *chip_reset = (volatile uint32_t*)0x4010002C;
    uint32_t reset_reason = *chip_reset;
    printf("Reset reason: 0x%08lX\n", (unsigned long)reset_reason);
    if (reset_reason & (1 << 28)) printf("  - Watchdog reset (RSM)\n");
    if (reset_reason & (1 << 27)) printf("  - Hazard sys reset request\n");
    if (reset_reason & (1 << 26)) printf("  - Glitch detect!\n");
    if (reset_reason & (1 << 25)) printf("  - SW core power down\n");
    if (reset_reason & (1 << 24)) printf("  - Watchdog reset (SWCORE)\n");
    if (reset_reason & (1 << 23)) printf("  - Watchdog reset (powman async)\n");
    if (reset_reason & (1 << 22)) printf("  - Watchdog reset (powman)\n");
    if (reset_reason & (1 << 21)) printf("  - DP reset request\n");
    if (reset_reason & (1 << 17)) printf("  - Rescue reset\n");
    if (reset_reason & (1 << 4))  printf("  - Double tap\n");
    if (reset_reason & (1 << 0))  printf("  - POR (power-on reset)\n");
    if (watchdog_caused_reboot()) {
        printf("*** pico SDK: WATCHDOG RESET ***\n");
    }

    printf("=====================================\n");
    printf("  MurmC64 - C64 Emulator (Frodo4)\n");
    printf("  RP2350 Port\n");
    printf("=====================================\n");
    printf("Board variant: %s\n",
#ifdef BOARD_M1
           "M1"
#elif BOARD_PC
           "PC"
#else
           "M2"
#endif
    );
    printf("CPU: %d MHz, PSRAM: %d MHz\n", CPU_CLOCK_MHZ, PSRAM_MAX_FREQ_MHZ);
    printf("\n");
#endif
}

static void init_psram(void) {
    MII_DEBUG_PRINTF("Initializing PSRAM...\n");

    uint psram_pin = get_psram_pin();
    MII_DEBUG_PRINTF("PSRAM CS pin: %u\n", psram_pin);

    psram_init(psram_pin);

    // Test PSRAM access
    volatile uint8_t *psram = (volatile uint8_t *)0x11000000;
    psram[0] = 0xAA;
    psram[1] = 0x55;
    if (psram[0] == 0xAA && psram[1] == 0x55) {
        MII_DEBUG_PRINTF("PSRAM test: OK\n");
    } else {
        MII_DEBUG_PRINTF("PSRAM test: FAILED!\n");
    }

    // Reset PSRAM allocator
    psram_reset();
}

static void init_graphics(void) {
    MII_DEBUG_PRINTF("Initializing HDMI graphics...\n");

    // Defer IRQ setup to Core 1
    MII_DEBUG_PRINTF("  Setting defer IRQ to Core 1...\n");
    #ifdef VIDEO_HDMI
    graphics_set_defer_irq_to_core1(true);
    #endif

    // Initialize HDMI
    MII_DEBUG_PRINTF("  Calling graphics_init(g_out_HDMI)...\n");
    graphics_init(g_out_HDMI);
    MII_DEBUG_PRINTF("  graphics_init done\n");

    // Set resolution
    MII_DEBUG_PRINTF("  Setting resolution %dx%d...\n", FB_WIDTH, FB_HEIGHT);
    graphics_set_res(FB_WIDTH, FB_HEIGHT);

    // Set initial framebuffer
    MII_DEBUG_PRINTF("  Setting initial framebuffer at %p...\n", (void*)g_front_buffer);
    graphics_set_buffer(g_front_buffer);

    // Clear framebuffers to black
    memset(g_framebuffer_a, 0, sizeof(g_framebuffer_a));
    memset(g_framebuffer_b, 0, sizeof(g_framebuffer_b));

    MII_DEBUG_PRINTF("Graphics initialized: %dx%d\n", FB_WIDTH, FB_HEIGHT);
}

static void init_c64_palette(void) {
    MII_DEBUG_PRINTF("Setting C64 color palette...\n");
#if 0
    static const uint32_t c64_palette[16] = {
        0x000000, // 0 Black        (0,0,0)
        0xFCFCFC, // 1 White        (255→252)
        0x542A2A, // 2 Red          (104→84, 55→42, 43→42)
        0x5484A8, // 3 Cyan         (112→84, 164→168, 178→168)
        0x542A84, // 4 Purple       (111→84, 61→42, 134→168)
        0x548454, // 5 Green        (88→84, 141→168, 67→84)
        0x2A2A84, // 6 Blue         (53→42, 40→42, 121→84)
        0xA8A884, // 7 Yellow       (184→168, 199→168, 111→84)
        0x844200, // 8 Orange       (111→84, 79→42, 37→0)
        0x422A00, // 9 Brown        (67→42, 57→42, 0)
        0x845454, // 10 Light Red   (154→168, 103→84, 89→84)
        0x2A2A2A, // 11 Dark Grey   (68→42)
        0x545454, // 12 Grey        (108→84)
        0xA8D454, // 13 Light Green (154→168, 210→252, 132→84)
        0x5454D4, // 14 Light Blue  (108→84, 94→84, 181→168)
        0xA8A8A8  // 15 Light Grey  (149→168)
    };
#else
    // C64 "Pepto" palette (accurate colors based on measurements)
    // These are the 16 C64 colors
    static const uint32_t c64_palette[16] = {
        0x000000,  // 0 - Black
        0xFFFFFF,  // 1 - White
        0x68372B,  // 2 - Red
        0x70A4B2,  // 3 - Cyan
        0x6F3D86,  // 4 - Purple
        0x588D43,  // 5 - Green
        0x352879,  // 6 - Blue
        0xB8C76F,  // 7 - Yellow
        0x6F4F25,  // 8 - Orange
        0x433900,  // 9 - Brown
        0x9A6759,  // 10 - Light Red
        0x444444,  // 11 - Dark Grey
        0x6C6C6C,  // 12 - Grey
        0x9AD284,  // 13 - Light Green
        0x6C5EB5,  // 14 - Light Blue
        0x959595,  // 15 - Light Grey
    };
#endif
    // Set up C64 colors (indices 0-15)
    for (int i = 0; i < 16; i++) {
        graphics_set_palette(i, c64_palette[i]);
    }

    // UI colors (16-21)
    graphics_set_palette(16, 0xD0D0D0);  // fill_gray
    graphics_set_palette(17, 0xF0F0F0);  // shine_gray
    graphics_set_palette(18, 0x404040);  // shadow_gray
    graphics_set_palette(19, 0xF00000);  // red
    graphics_set_palette(20, 0x300000);  // dark_red
    graphics_set_palette(21, 0x00C000);  // green

    // Fill remaining palette with grayscale
    for (int i = 22; i < 240; i++) {
        uint8_t gray = (i * 255) / 239;
        graphics_set_palette(i, (gray << 16) | (gray << 8) | gray);
    }
}

static void init_input(void) {
    MII_DEBUG_PRINTF("Initializing input devices...\n");

    // Initialize all input (PS/2, USB HID, gamepad, key matrices)
    input_rp2350_init();

    MII_DEBUG_PRINTF("Input initialized\n");
}

static void init_storage(void) {
    MII_DEBUG_PRINTF("Initializing SD card...\n");

    // Initialize SD card - FATFS must be static to persist after function returns!
    static FATFS fatfs;
    FRESULT fr = f_mount(&fatfs, "", 1);
    if (fr != FR_OK) {
        MII_DEBUG_PRINTF("SD card mount failed: %d\n", fr);
        MII_DEBUG_PRINTF("Continuing without disk support...\n");
    } else {
        MII_DEBUG_PRINTF("SD card mounted OK\n");
        f_mkdir("/c64");
        // Scan for disk images
        disk_loader_init();
        disk_loader_scan();
    }
}

static void init_audio(void) {
    MII_DEBUG_PRINTF("Initializing I2S audio...\n");
    sid_i2s_init();
    MII_DEBUG_PRINTF("I2S audio initialized (DATA=%d, CLK=%d/%d, %d Hz)\n",
           I2S_DATA_PIN, I2S_CLOCK_PIN_BASE, I2S_CLOCK_PIN_BASE + 1, SID_SAMPLE_RATE);
}

//=============================================================================
// Fault Handler (for debugging crashes)
//=============================================================================

// Override HardFault handler to print diagnostics
void __attribute__((naked)) HardFault_Handler(void) {
    __asm volatile(
        "mrs r0, msp\n"
        "b hard_fault_handler_c\n"
    );
}

void hard_fault_handler_c(uint32_t *stack) {
    uint32_t r0 = stack[0];
    uint32_t r1 = stack[1];
    uint32_t r2 = stack[2];
    uint32_t r3 = stack[3];
    uint32_t r12 = stack[4];
    uint32_t lr = stack[5];
    uint32_t pc = stack[6];
    uint32_t psr = stack[7];

    printf("\n!!! HARD FAULT !!!\n");
    printf("PC=0x%08lX LR=0x%08lX PSR=0x%08lX\n",
           (unsigned long)pc, (unsigned long)lr, (unsigned long)psr);
    printf("R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX\n",
           (unsigned long)r0, (unsigned long)r1, (unsigned long)r2, (unsigned long)r3);
    printf("R12=0x%08lX SP=0x%08lX\n",
           (unsigned long)r12, (unsigned long)(uint32_t)stack);

    // Blink LED or hang forever
    while (1) {
        tight_loop_contents();
    }
}

//=============================================================================
// Stack Monitoring (for debugging crashes)
//=============================================================================

// Get current stack pointer
static inline uint32_t get_stack_pointer(void) {
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

// Check stack usage and warn if low
static void check_stack(const char *location) {
    static uint32_t min_sp_core0 = 0xFFFFFFFF;
    static uint32_t min_sp_core1 = 0xFFFFFFFF;

    uint32_t sp = get_stack_pointer();
    uint32_t core = get_core_num();

    if (core == 0) {
        if (sp < min_sp_core0) {
            min_sp_core0 = sp;
            // Warn if stack is getting low (arbitrary threshold)
            if (sp < 0x20001000) {
                printf("!!! Core0 stack LOW at %s: SP=0x%08lX\n", location, (unsigned long)sp);
            }
        }
    } else {
        if (sp < min_sp_core1) {
            min_sp_core1 = sp;
            if (sp < 0x20001000) {
                printf("!!! Core1 stack LOW at %s: SP=0x%08lX\n", location, (unsigned long)sp);
            }
        }
    }
}

//=============================================================================
// Main Emulator Loop
//=============================================================================

static void emulator_main_loop(void) {
    MII_DEBUG_PRINTF("Starting C64 emulator...\n");
    check_stack("emulator_main_loop start");

    // Set up framebuffer globals for Display_rp2350.cpp
    framebuffers[0] = g_framebuffer_a;
    framebuffers[1] = g_framebuffer_b;
    current_framebuffer = g_back_buffer;
    current_fb_index = 1;

    // Initialize C64 emulator
    MII_DEBUG_PRINTF("Calling c64_init()...\n");
    c64_init();
    MII_DEBUG_PRINTF("c64_init() returned\n");

    // Signal to Core 1 that emulator is ready
    g_emulator_ready = true;
    MII_DEBUG_PRINTF("Signaled Core 1, entering main loop...\n");

    // Main emulation loop
    uint32_t frame_count = 0;
    uint32_t total_frames = 0;
    bool first_frame = true;
    uint32_t last_time = rp2350_get_ticks_ms();

    // Frame timing: PAL = 50 Hz = 20000 us per frame
    const uint32_t FRAME_TIME_US = 20000;
    uint64_t next_frame_time = rp2350_get_ticks_us();

    // Watchdog DISABLED for debugging
    // watchdog_enable(2000, true);

    while (!g_quit_requested) {
        // Feed watchdog at start of each frame
        // watchdog_update();

        // Run one frame of C64 emulation
        if (first_frame) MII_DEBUG_PRINTF("Running first frame...\n");
        c64_run_frame();
        if (first_frame) { MII_DEBUG_PRINTF("First frame done\n"); first_frame = false; }

        // Swap framebuffers
        uint8_t *temp = g_front_buffer;
        g_front_buffer = g_back_buffer;
        g_back_buffer = temp;
        current_fb_index = (current_fb_index + 1) % 2;
        current_framebuffer = g_back_buffer;

        // Request buffer swap at next vsync (thread-safe)
        graphics_request_buffer_swap(g_front_buffer);

        // Update audio (SID -> I2S)
        sid_i2s_update();

        frame_count++;
        total_frames++;

        // Frame pacing: wait until it's time for the next frame
        // This ensures the emulation runs at exactly 50 fps (PAL)
        next_frame_time += FRAME_TIME_US;
        uint64_t now_us = rp2350_get_ticks_us();

        if (now_us < next_frame_time) {
            // Emulation is faster than real-time, wait
            uint32_t wait_us = (uint32_t)(next_frame_time - now_us);
            if (wait_us > 1000) {
                // Sleep for most of the wait time (leave 1ms for precision)
                sleep_us(wait_us - 1000);
            }
            // Spin-wait for the remaining time for precise timing
            while (rp2350_get_ticks_us() < next_frame_time) {
                tight_loop_contents();
            }
        } else {
            // Emulation is slower than real-time, skip frame pacing
            // Reset timing to avoid accumulating lag
            if (now_us > next_frame_time + FRAME_TIME_US * 2) {
                next_frame_time = now_us;
            }
        }

        // FPS tracking (silent)
        uint32_t now = rp2350_get_ticks_ms();
        if (now - last_time >= 1000) {
            frame_count = 0;
            last_time = now;
        }
    }

    watchdog_disable();
}

//=============================================================================
// Main Entry Point
//=============================================================================

int main(void) {
    // Initialize system clocks first
    init_clocks();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    for (int i = 0; i < 6; i++) {
        sleep_ms(33);
        gpio_put(PICO_DEFAULT_LED_PIN, true);
        sleep_ms(33);
        gpio_put(PICO_DEFAULT_LED_PIN, false);
    }
#endif

    // Initialize stdio for debug output
    init_stdio();

#ifdef PSRAM_MAX_FREQ_MHZ
    // Initialize PSRAM
    init_psram();
#endif
    // Initialize graphics (HDMI)
    init_graphics();

    // Set C64 color palette
    init_c64_palette();

#ifdef VIDEO_HDMI
    // Launch Core 1 for video rendering (needed for HDMI output)
    MII_DEBUG_PRINTF("Launching Core 1...\n");
    multicore_launch_core1(core1_video_task);
    sleep_ms(100);  // Let Core 1 initialize HDMI IRQ
#endif

    // Initialize framebuffer pointers for double buffering (needed by startscreen)
    framebuffers[0] = g_framebuffer_a;
    framebuffers[1] = g_framebuffer_b;

    // Show start screen with system information
    {
        startscreen_info_t screen_info = {
            .title = "MurmC64",
            .subtitle = "Commodore 64 Emulator",
            .version = FIRMWARE_VERSION,
#ifdef BOARD_M1
            .board_variant = "M1",
#endif
#ifdef BOARD_M2
            .board_variant = "M2",
#endif
#ifdef BOARD_PC
            .board_variant = "PICO PC",
#endif
#ifdef BOARD_Z0
            .board_variant = "PiZero",
#endif
            .cpu_mhz = CPU_CLOCK_MHZ,
#ifdef PSRAM_MAX_FREQ_MHZ
            .psram_mhz = PSRAM_MAX_FREQ_MHZ,
#endif
        };
        startscreen_show(&screen_info);
    }

    // Initialize input devices
    init_input();

    // Initialize storage (SD card)
    init_storage();

    // Initialize audio (I2S)
    init_audio();

    // Run emulator on Core 0
    emulator_main_loop();

    // Cleanup (shouldn't reach here normally)
    MII_DEBUG_PRINTF("Emulator exiting...\n");

    return 0;
}

//=============================================================================
// Utility Functions for Time (replacing SDL)
//=============================================================================

static uint32_t rp2350_get_ticks_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint64_t rp2350_get_ticks_us(void) {
    return to_us_since_boot(get_absolute_time());
}

__weak
void _unlink(const char* n) {}