/*
 * murmgenesis - I2S Audio Driver with Chained Double Buffer DMA
 *
 * Uses two DMA channels in ping-pong configuration:
 * - Channel A plays buffer 0, then triggers channel B
 * - Channel B plays buffer 1, then triggers channel A
 *
 * Each channel completion raises DMA_IRQ_1; the IRQ handler re-arms the
 * completed channel (reset read addr + transfer count) and marks its buffer
 * free for the CPU to refill.
 */

// Target samples per frame for consistent DMA timing
// C64 PAL: 44100 / 50 = 882, NTSC: 44100 / 60 = 735
#define TARGET_SAMPLES_PAL 882

#include "audio.h"
#include "board_config.h"
#if FEATURE_AUDIO_I2S
#include "audio_i2s.pio.h"
#endif
#if defined(FEATURE_AUDIO_PWM)
#include <hardware/pwm.h>
#define PWM_BITS 12
#define PWM_WRAP ((1 << PWM_BITS) - 1)
#define PWM_OSR               16
#define PWM_AUDIO_RATE        AUDIO_SAMPLE_RATE
#define PWM_DMA_SAMPLES       TARGET_SAMPLES_PAL
#endif

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"  // For memory barriers
#include "hardware/irq.h"
#include "hardware/resets.h"  // For reset_block

//=============================================================================
// State - Chained double buffer (ping-pong) DMA
//=============================================================================

// NOTE: HDMI uses DMA_IRQ_0 with an exclusive handler.
// Audio uses DMA_IRQ_1 to avoid conflicts.

#define AUDIO_DMA_IRQ DMA_IRQ_1

// Fixed DMA channels for audio (keep away from dynamically-claimed HDMI channels)
#define AUDIO_DMA_CH_A 10
#define AUDIO_DMA_CH_B 11

#define DMA_BUFFER_COUNT 2
// One DMA word is one stereo frame (packed L/R int16).
// AUDIO_BUFFER_SAMPLES is sized to cover NTSC/PAL with headroom.
#define DMA_BUFFER_MAX_SAMPLES AUDIO_BUFFER_SAMPLES

static uint32_t __attribute__((aligned(4))) dma_buffers[DMA_BUFFER_COUNT][DMA_BUFFER_MAX_SAMPLES];

// Bitmask of buffers the CPU is allowed to write (1 = free)
static volatile uint32_t dma_buffers_free_mask = 0;

// Pre-roll: fill both buffers before starting playback
#define PREROLL_BUFFERS 2
static volatile int preroll_count = 0;

static int dma_channel_a = -1;
static int dma_channel_b = -1;
static PIO audio_pio;
static uint audio_sm;
static uint32_t dma_transfer_count;

static volatile bool audio_running = false;

static void audio_dma_irq_handler(void);

//=============================================================================
// I2S Implementation
//=============================================================================

#if defined(FEATURE_AUDIO_I2S)
i2s_config_t i2s_get_default_config(void) {
    // Use 882 samples per frame for C64 PAL (44100 Hz / 50 fps)
    i2s_config_t config = {
        .sample_freq = AUDIO_SAMPLE_RATE,
        .channel_count = 2,
        .data_pin = I2S_DATA_PIN,
        .clock_pin_base = I2S_CLOCK_PIN_BASE,
        .pio = pio0,
        .sm = 0,
        .dma_channel = 0,
        .dma_trans_count = 882,
        .dma_buf = NULL,
        .volume = 0,
    };
    return config;
}
#endif
#if defined(FEATURE_AUDIO_PWM)
static int g_pwm_dma_chan = -1;
static uint g_pwm_slice = 0;

// DMA буфер: по одному 32-бит слову на сэмпл (L в low16, R в high16)
static uint32_t *g_pwm_dma_buf = NULL;
static uint32_t g_pwm_dma_count = 0;
static bool g_pwm_dma_active = false;
#ifdef BOARD_PC
// PCp2: right GPIO27 = slice 5 B, left GPIO28 = slice 6 A. The two pins are on
// different slices, so a second DMA channel feeds the left slice from the same
// buffer (its CC low half = channel A = left, same packing as on M1/M2).
static int g_pwm_dma_chan2 = -1;
static uint g_pwm_slice2 = 0;
#endif
#endif

void i2s_init(i2s_config_t *config) {
#if ENABLE_DEBUG_LOGS
    printf("Audio: Initializing I2S with chained double-buffer DMA...\n");
    printf("Audio: Sample rate: %u Hz, DMA buffer size: %lu frames\n",
           (unsigned)config->sample_freq, (unsigned long)config->dma_trans_count);
#endif

#if defined(FEATURE_AUDIO_PWM)
    // PWM pins must be adjacent for single-slice stereo via CC
    gpio_set_function(PWM_RIGHT_PIN, GPIO_FUNC_PWM);
    gpio_set_function(PWM_LEFT_PIN,  GPIO_FUNC_PWM);

    // Both pins (10/11) share the same slice
    g_pwm_slice = pwm_gpio_to_slice_num(PWM_RIGHT_PIN);
    pwm_config pcfg = pwm_get_default_config();
    // PWM frequency = clk_sys / (clkdiv * (PWM_WRAP + 1))
    uint32_t sys_clk = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_clk / (float)(PWM_AUDIO_RATE * (PWM_WRAP + 1));
    pwm_config_set_clkdiv(&pcfg, clkdiv);
    pwm_config_set_wrap(&pcfg, PWM_WRAP);
    pwm_init(g_pwm_slice, &pcfg, true);
    // Enable both PWM channels explicitly
    pwm_set_chan_level(g_pwm_slice, PWM_CHAN_A, PWM_WRAP >> 1);
    pwm_set_chan_level(g_pwm_slice, PWM_CHAN_B, PWM_WRAP >> 1);
    pwm_set_enabled(g_pwm_slice, true);

#ifdef BOARD_PC
    g_pwm_slice2 = pwm_gpio_to_slice_num(PWM_LEFT_PIN);
    pwm_init(g_pwm_slice2, &pcfg, false);
    pwm_set_chan_level(g_pwm_slice2, PWM_CHAN_A, PWM_WRAP >> 1);
    pwm_set_chan_level(g_pwm_slice2, PWM_CHAN_B, PWM_WRAP >> 1);
    // restart both slices in phase so their DREQs pace the DMAs together
    pwm_set_enabled(g_pwm_slice, false);
    pwm_set_counter(g_pwm_slice, 0);
    pwm_set_counter(g_pwm_slice2, 0);
    pwm_set_mask_enabled((1u << g_pwm_slice) | (1u << g_pwm_slice2));
#endif

    // init duty to mid
    pwm_set_gpio_level(PWM_RIGHT_PIN, PWM_WRAP >> 1);
    pwm_set_gpio_level(PWM_LEFT_PIN,  PWM_WRAP >> 1);

    // Allocate DMA buffer sized to ONE audio buffer worth of frames
    static uint32_t dma_buf[PWM_DMA_SAMPLES];
    g_pwm_dma_count = PWM_DMA_SAMPLES;
    g_pwm_dma_buf = dma_buf;

    g_pwm_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config dcfg = dma_channel_get_default_config(g_pwm_dma_chan);
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dcfg, true);
    channel_config_set_write_increment(&dcfg, false);
    channel_config_set_dreq(&dcfg, pwm_get_dreq(g_pwm_slice));

    // Destination = PWM CC register (writes both A/B in one 32-bit word)
    dma_channel_configure(
        g_pwm_dma_chan,
        &dcfg,
        &pwm_hw->slice[g_pwm_slice].cc,
        g_pwm_dma_buf,
        g_pwm_dma_count,
        false
    );
#ifdef BOARD_PC
    g_pwm_dma_chan2 = dma_claim_unused_channel(true);
    dma_channel_config dcfg2 = dma_channel_get_default_config(g_pwm_dma_chan2);
    channel_config_set_transfer_data_size(&dcfg2, DMA_SIZE_32);
    channel_config_set_read_increment(&dcfg2, true);
    channel_config_set_write_increment(&dcfg2, false);
    channel_config_set_dreq(&dcfg2, pwm_get_dreq(g_pwm_slice2));
    dma_channel_configure(
        g_pwm_dma_chan2,
        &dcfg2,
        &pwm_hw->slice[g_pwm_slice2].cc,
        g_pwm_dma_buf,
        g_pwm_dma_count,
        false
    );
#endif
#endif
#if defined(FEATURE_AUDIO_I2S)
    audio_pio = config->pio;
    dma_transfer_count = config->dma_trans_count;

    // NOTE: Do NOT reset PIO0 here - PS/2 keyboard uses PIO0!
    // Just clear DMA IRQ flags instead.

    // Clear audio DMA IRQ flags (IRQ1)
    dma_hw->ints1 = (1u << AUDIO_DMA_CH_A) | (1u << AUDIO_DMA_CH_B);

    // Configure GPIO for PIO
    gpio_set_function(config->data_pin, GPIO_FUNC_PIO0);
    gpio_set_function(config->clock_pin_base, GPIO_FUNC_PIO0);
    gpio_set_function(config->clock_pin_base + 1, GPIO_FUNC_PIO0);

    gpio_set_drive_strength(config->data_pin, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(config->clock_pin_base, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(config->clock_pin_base + 1, GPIO_DRIVE_STRENGTH_12MA);

    // Claim state machine
    audio_sm = pio_claim_unused_sm(audio_pio, true);
    config->sm = audio_sm;
#if ENABLE_DEBUG_LOGS
    printf("Audio: Using PIO0 SM%d\n", audio_sm);
#endif

    // Add PIO program
    uint offset = pio_add_program(audio_pio, &audio_i2s_program);
    audio_i2s_program_init(audio_pio, audio_sm, offset,
                           config->data_pin, config->clock_pin_base);

    // Drain the TX FIFO
    pio_sm_clear_fifos(audio_pio, audio_sm);

    // Set clock divider for sample rate
    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t divider = sys_clk * 4 / config->sample_freq;
    pio_sm_set_clkdiv_int_frac(audio_pio, audio_sm, divider >> 8u, divider & 0xffu);
#if ENABLE_DEBUG_LOGS
        printf("Audio: Clock divider: %u.%u (sys=%lu MHz)\n",
            (unsigned)(divider >> 8u), (unsigned)(divider & 0xffu), (unsigned long)(sys_clk / 1000000));
#endif

    // Validate transfer count fits our static buffers
    dma_transfer_count = config->dma_trans_count;
    if (dma_transfer_count == 0) dma_transfer_count = 1;
    if (dma_transfer_count > DMA_BUFFER_MAX_SAMPLES) dma_transfer_count = DMA_BUFFER_MAX_SAMPLES;
    config->dma_trans_count = (uint16_t)dma_transfer_count;

    // Initialize DMA buffers with silence
    memset(dma_buffers, 0, sizeof(dma_buffers));
    config->dma_buf = (uint16_t *)(void *)dma_buffers[0];

    // Use fixed DMA channels for audio
    dma_channel_abort(AUDIO_DMA_CH_A);
    dma_channel_abort(AUDIO_DMA_CH_B);
    while (dma_channel_is_busy(AUDIO_DMA_CH_A) || dma_channel_is_busy(AUDIO_DMA_CH_B)) {
        tight_loop_contents();
    }

    dma_channel_unclaim(AUDIO_DMA_CH_A);
    dma_channel_unclaim(AUDIO_DMA_CH_B);
    dma_channel_claim(AUDIO_DMA_CH_A);
    dma_channel_claim(AUDIO_DMA_CH_B);
    dma_channel_a = AUDIO_DMA_CH_A;
    dma_channel_b = AUDIO_DMA_CH_B;
    config->dma_channel = (uint8_t)dma_channel_a;
#if ENABLE_DEBUG_LOGS
    printf("Audio: Using DMA channels %d/%d (IRQ=%d)\n", dma_channel_a, dma_channel_b, AUDIO_DMA_IRQ);
#endif

    // Configure DMA channels in ping-pong chain
    dma_channel_config cfg_a = dma_channel_get_default_config(dma_channel_a);
    channel_config_set_read_increment(&cfg_a, true);
    channel_config_set_write_increment(&cfg_a, false);
    channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_32);
    channel_config_set_dreq(&cfg_a, pio_get_dreq(audio_pio, audio_sm, true));
    channel_config_set_chain_to(&cfg_a, dma_channel_b);

    dma_channel_config cfg_b = dma_channel_get_default_config(dma_channel_b);
    channel_config_set_read_increment(&cfg_b, true);
    channel_config_set_write_increment(&cfg_b, false);
    channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_32);
    channel_config_set_dreq(&cfg_b, pio_get_dreq(audio_pio, audio_sm, true));
    channel_config_set_chain_to(&cfg_b, dma_channel_a);

    dma_channel_configure(
        dma_channel_a,
        &cfg_a,
        &audio_pio->txf[audio_sm],
        dma_buffers[0],
        dma_transfer_count,
        false
    );

    dma_channel_configure(
        dma_channel_b,
        &cfg_b,
        &audio_pio->txf[audio_sm],
        dma_buffers[1],
        dma_transfer_count,
        false
    );

    // Set up DMA IRQ1 handler (avoid HDMI's DMA_IRQ_0 exclusive handler)
    irq_set_exclusive_handler(AUDIO_DMA_IRQ, audio_dma_irq_handler);
    irq_set_priority(AUDIO_DMA_IRQ, 0x80);
    irq_set_enabled(AUDIO_DMA_IRQ, true);

    // Enable IRQ1 for both channels
    dma_hw->ints1 = (1u << dma_channel_a) | (1u << dma_channel_b);
    dma_channel_set_irq1_enabled(dma_channel_a, true);
    dma_channel_set_irq1_enabled(dma_channel_b, true);

    // Enable PIO state machine
    pio_sm_set_enabled(audio_pio, audio_sm, true);

    // Initialize state
    preroll_count = 0;
    dma_buffers_free_mask = (1u << DMA_BUFFER_COUNT) - 1u; // both free
#endif
    audio_running = false;

#if ENABLE_DEBUG_LOGS
    printf("Audio: I2S ready (double buffer DMA with %d buffer pre-roll)\n", PREROLL_BUFFERS);
#endif
}

#if FEATURE_AUDIO_PWM
static inline uint16_t s16_to_pwm_u16(int16_t s) {
    int32_t v = (int32_t)s + 32768;      // 0..65535
    v >>= (16 - PWM_BITS);               // -> 0..PWM_WRAP
    if (v < 0) v = 0;
    if (v > PWM_WRAP) v = PWM_WRAP;
    return (uint16_t)v;
}

static inline uint32_t pack_pwm_cc(uint16_t left, uint16_t right) {
    return ((uint32_t)right << 16) | left;
}

void pwm_dma_write_count(const int16_t *samples,
                         uint32_t sample_count)
{
    if (!samples || !g_pwm_dma_buf || g_pwm_dma_chan < 0)
        return;

    if (sample_count > g_pwm_dma_count)
        sample_count = g_pwm_dma_count;

    // Дождаться завершения предыдущего DMA
    if (g_pwm_dma_active) {
        dma_channel_wait_for_finish_blocking(g_pwm_dma_chan);
#ifdef BOARD_PC
        dma_channel_wait_for_finish_blocking(g_pwm_dma_chan2);
#endif
        g_pwm_dma_active = false;
    }

    // Заполнить DMA буфер
    for (uint32_t i = 0; i < sample_count; i++) {
        int16_t l = samples[i * 2 + 0];
        int16_t r = samples[i * 2 + 1];
        g_pwm_dma_buf[i] =
            pack_pwm_cc(s16_to_pwm_u16(l),
                        s16_to_pwm_u16(r));
    }

    // Добить остаток тишиной
    uint32_t mid = pack_pwm_cc(PWM_WRAP >> 1, PWM_WRAP >> 1);
    for (uint32_t i = sample_count; i < g_pwm_dma_count; i++) {
        g_pwm_dma_buf[i] = mid;
    }

    __dmb();

    dma_channel_set_read_addr(g_pwm_dma_chan, g_pwm_dma_buf, false);
    dma_channel_set_trans_count(g_pwm_dma_chan, g_pwm_dma_count, false);
#ifdef BOARD_PC
    dma_channel_set_read_addr(g_pwm_dma_chan2, g_pwm_dma_buf, false);
    dma_channel_set_trans_count(g_pwm_dma_chan2, g_pwm_dma_count, false);
    dma_start_channel_mask((1u << g_pwm_dma_chan) | (1u << g_pwm_dma_chan2));
#else
    dma_channel_start(g_pwm_dma_chan);
#endif

    g_pwm_dma_active = true;
}

void i2s_dma_write(i2s_config_t *config, const int16_t *samples) {
    i2s_dma_write_count(config, samples, TARGET_SAMPLES_PAL);
}
#endif

#if defined(FEATURE_AUDIO_I2S)
void i2s_dma_write_count(i2s_config_t *config, const int16_t *samples, uint32_t sample_count) {
    if (sample_count > dma_transfer_count) sample_count = dma_transfer_count;
    if (sample_count == 0) sample_count = 1;

    // Wait for a free buffer, then claim it (atomically vs DMA IRQ)
    uint8_t buf_index = 0;
    while (true) {
        uint32_t irq_state = save_and_disable_interrupts();
        uint32_t free_mask = dma_buffers_free_mask;

        if (!audio_running) {
            // Pre-roll fills buffer 0 then buffer 1 to preserve ordering
            buf_index = (uint8_t)preroll_count;
            if (buf_index < DMA_BUFFER_COUNT && (free_mask & (1u << buf_index))) {
                dma_buffers_free_mask &= ~(1u << buf_index);
                restore_interrupts(irq_state);
                break;
            }
        } else {
            if (free_mask) {
                buf_index = (free_mask & 1u) ? 0 : 1;
                dma_buffers_free_mask &= ~(1u << buf_index);
                restore_interrupts(irq_state);
                break;
            }
        }

        restore_interrupts(irq_state);
        tight_loop_contents();
    }

    uint32_t *write_ptr = dma_buffers[buf_index];
    int16_t *write_ptr16 = (int16_t *)(void *)write_ptr;

    if (config->volume == 0) {
        memcpy(write_ptr, samples, sample_count * sizeof(uint32_t));
    } else {
        // Volume adjustment
        for (uint32_t i = 0; i < sample_count * 2; i++) {
            write_ptr16[i] = samples[i] >> config->volume;
        }
    }

    // Pad remainder with silence to keep DMA transfer size stable
    if (sample_count < dma_transfer_count) {
        memset(&write_ptr[sample_count], 0, (dma_transfer_count - sample_count) * sizeof(uint32_t));
    }

    // Memory barrier to ensure writes are visible before DMA reads
    __dmb();

    if (!audio_running) {
        preroll_count++;
        if (preroll_count >= PREROLL_BUFFERS) {
            // Both buffers are filled and queued; start playback on channel A
            dma_channel_start(dma_channel_a);
            audio_running = true;
        }
    }
}

void i2s_dma_write(i2s_config_t *config, const int16_t *samples) {
    i2s_dma_write_count(config, samples, dma_transfer_count);
}

void i2s_volume(i2s_config_t *config, uint8_t volume) {
    if (volume > 16) volume = 16;
    config->volume = volume;
}

void i2s_increase_volume(i2s_config_t *config) {
    if (config->volume > 0) config->volume--;
}

void i2s_decrease_volume(i2s_config_t *config) {
    if (config->volume < 16) config->volume++;
}
#endif

//=============================================================================
// High-level Audio API
//=============================================================================

static bool audio_initialized = false;
static bool audio_enabled = true;
static int master_volume = 100;  // 0-128
#if defined(FEATURE_AUDIO_I2S)
static i2s_config_t i2s_config;
#endif

// Startup mute: output silence for first N frames to let hardware settle
#define STARTUP_FADE_FRAMES 120  // 2 seconds at 60fps
static int startup_frame_counter = 0;

// Low-pass filter state (declared here so audio_init can reset it)
static int32_t lpf_state = 0;

// Mixed stereo buffer
static int16_t __attribute__((aligned(4))) mixed_buffer[AUDIO_BUFFER_SAMPLES * 2];

static inline int16_t clamp_s16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

bool audio_init(void) {
    // ALWAYS reinitialize - don't trust static state after hard reset
    // Reset all state variables first
    audio_initialized = false;
    audio_running = false;
    lpf_state = 0;
    dma_channel_a = -1;
    dma_channel_b = -1;
    preroll_count = 0;
    dma_buffers_free_mask = (1u << DMA_BUFFER_COUNT) - 1u;
    startup_frame_counter = 0;

#if defined(FEATURE_AUDIO_I2S)
    i2s_config = i2s_get_default_config();
    i2s_init(&i2s_config);
#endif
#if defined(FEATURE_AUDIO_PWM)
    i2s_config_t cfg = {
        .sample_freq = AUDIO_SAMPLE_RATE,
        .channel_count = 2,
        .dma_trans_count = TARGET_SAMPLES_PAL,
    };
    i2s_init(&cfg);
#endif
    audio_initialized = true;
    lpf_state = 0;

    return true;
}

void audio_shutdown(void) {
    if (!audio_initialized) return;

    // Stop producing new audio and stop the PIO state machine first
    audio_running = false;

#if defined(FEATURE_AUDIO_PWM)
    if (g_pwm_dma_chan >= 0) {
        dma_channel_abort(g_pwm_dma_chan);
        dma_channel_unclaim(g_pwm_dma_chan);
        g_pwm_dma_chan = -1;
#ifdef BOARD_PC
        dma_channel_abort(g_pwm_dma_chan2);
        dma_channel_unclaim(g_pwm_dma_chan2);
        g_pwm_dma_chan2 = -1;
#endif
        g_pwm_dma_active = false;
    }
#endif
#if defined(FEATURE_AUDIO_I2S)
    pio_sm_set_enabled(audio_pio, audio_sm, false);

    // Disable DMA IRQ and per-channel IRQ generation (audio uses IRQ1)
    irq_set_enabled(AUDIO_DMA_IRQ, false);

    if (dma_channel_a >= 0) {
        dma_channel_set_irq1_enabled(dma_channel_a, false);
        dma_channel_abort(dma_channel_a);
        // Clear any pending IRQ flag
        dma_hw->ints1 = (1u << dma_channel_a);
        dma_channel_unclaim(dma_channel_a);
        dma_channel_a = -1;
    }

    if (dma_channel_b >= 0) {
        dma_channel_set_irq1_enabled(dma_channel_b, false);
        dma_channel_abort(dma_channel_b);
        // Clear any pending IRQ flag
        dma_hw->ints1 = (1u << dma_channel_b);
        dma_channel_unclaim(dma_channel_b);
        dma_channel_b = -1;
    }

    // Mark both buffers free again
    dma_buffers_free_mask = (1u << DMA_BUFFER_COUNT) - 1u;
    preroll_count = 0;
#endif
    audio_initialized = false;
}

bool audio_is_initialized(void) {
    return audio_initialized;
}

static void audio_dma_irq_handler(void) {
    uint32_t ints = dma_hw->ints1;
    uint32_t mask = 0;
    if (dma_channel_a >= 0) mask |= (1u << dma_channel_a);
    if (dma_channel_b >= 0) mask |= (1u << dma_channel_b);
    ints &= mask;
    if (!ints) return;

    if ((dma_channel_a >= 0) && (ints & (1u << dma_channel_a))) {
        dma_hw->ints1 = (1u << dma_channel_a);
        dma_channel_set_read_addr(dma_channel_a, dma_buffers[0], false);
        dma_channel_set_trans_count(dma_channel_a, dma_transfer_count, false);
        dma_buffers_free_mask |= 1u;
    }

    if ((dma_channel_b >= 0) && (ints & (1u << dma_channel_b))) {
        dma_hw->ints1 = (1u << dma_channel_b);
        dma_channel_set_read_addr(dma_channel_b, dma_buffers[1], false);
        dma_channel_set_trans_count(dma_channel_b, dma_transfer_count, false);
        dma_buffers_free_mask |= 2u;
    }
}

void audio_submit(void) {
    if (!audio_initialized) return;

    // STARTUP MUTE: Output pure silence for first N frames
    if (startup_frame_counter < STARTUP_FADE_FRAMES) {
        memset(mixed_buffer, 0, TARGET_SAMPLES_PAL * 2 * sizeof(int16_t));
        startup_frame_counter++;
#if defined(FEATURE_AUDIO_I2S)
        i2s_dma_write_count(&i2s_config, mixed_buffer, TARGET_SAMPLES_PAL);
#endif
#if defined(FEATURE_AUDIO_PWM)
        pwm_dma_write_count(mixed_buffer, TARGET_SAMPLES_PAL);
#endif
        return;
    }

    // For now, output silence - SID integration will fill this buffer
    memset(mixed_buffer, 0, TARGET_SAMPLES_PAL * 2 * sizeof(int16_t));
#if defined(FEATURE_AUDIO_I2S)
    i2s_dma_write_count(&i2s_config, mixed_buffer, TARGET_SAMPLES_PAL);
#endif
}

void audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 128) volume = 128;
    master_volume = volume;
}

int audio_get_volume(void) {
    return master_volume;
}

void audio_set_enabled(bool enabled) {
    audio_enabled = enabled;
}

bool audio_is_enabled(void) {
    return audio_enabled;
}

void audio_flush_silence(void) {
    // Send a few frames of silence to clear the DMA buffer
    // This prevents the last audio sample from repeating
    if (!audio_initialized) return;

    for (int frame = 0; frame < 3; frame++) {
        for (int i = 0; i < TARGET_SAMPLES_PAL * 2; i++) {
            mixed_buffer[i] = 0;
        }
#if defined(FEATURE_AUDIO_I2S)
        i2s_dma_write_count(&i2s_config, mixed_buffer, TARGET_SAMPLES_PAL);
#endif
    }
}

void audio_debug_buffer_values(void) {
#if ENABLE_DEBUG_LOGS
    printf("Audio: buffer debug\n");
#endif
}

#if defined(FEATURE_AUDIO_I2S)
i2s_config_t* audio_get_i2s_config(void) {
    return &i2s_config;
}
#endif
