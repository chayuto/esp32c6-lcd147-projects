// =============================================================================
// board_config.h — Board-specific GPIO configuration for MCP GPIO Server
// =============================================================================
//
// PURPOSE
//   This file is the ONLY file you need to edit to port the MCP GPIO Server
//   to a different ESP32 board. Everything else (tool handlers, ADC reads,
//   HTTP server) reads from the tables defined here.
//
// HOW TO PORT TO A DIFFERENT BOARD
// ---------------------------------
//  1. Set BOARD_NAME to identify your board in tool responses.
//  2. Replace the SAFE_DIGITAL_PINS initializer with the GPIO numbers that
//     are free on your board (not used by LCD, SD, LEDs, USB, etc.).
//  3. Replace ADC_CAPABLE_PINS with the subset of your safe pins that are
//     wired to ADC1 channels on your chip.
//     On ESP32-C6: ADC1 = GPIO 0–6 only. There is no ADC2.
//     On original ESP32: ADC1 = GPIO 32–39, ADC2 = GPIO 0,2,4,12–15,25–27
//     (ADC2 is unusable while Wi-Fi is active on original ESP32).
//  4. Update g_reserved_pins[] to document the pins your board uses
//     internally. This table is returned verbatim by get_gpio_capabilities
//     so the LLM knows not to request those pins.
//  5. Set ADC_ATTENUATION to match the voltage range you expect on your board.
//     See the attenuation options in the comment below.
//
// =============================================================================

#pragma once

#include "esp_adc/adc_oneshot.h"   // adc1_channel_t, ADC_ATTEN_DB_*

// =============================================================================
// SECTION 1 — Board Identity
// Returned in the get_gpio_capabilities tool response so the LLM (and human
// reading logs) can confirm which board config is active.
// =============================================================================

// *** CHANGE THIS for your board ***
#define BOARD_NAME  "Waveshare ESP32-C6-LCD-1.47"

// =============================================================================
// SECTION 2 — Safe Digital I/O Pins
//
// These are the GPIO numbers exposed through all four MCP tools:
//   configure_pins, write_digital_pins, read_pins, get_gpio_capabilities
//
// RULES for choosing which pins to include:
//   - Do NOT include pins used by onboard peripherals (LCD, SD, LED, USB).
//   - Do NOT include USB D+/D- pins (GPIO 12/13 on ESP32-C6).
//   - Do NOT include pins that are not physically present on the header.
//   - Strapping pins (GPIO 4,5,8,9,15 on C6) are already reserved by this
//     board's onboard peripherals, so they are excluded automatically here.
//     If your board has free strapping pins, you CAN include them — they
//     behave as normal GPIO after boot — but note the risk in your comments.
//   - UART TX/RX (GPIO 16/17) are excluded here to preserve the serial
//     monitor. Include them if your application does not need serial debug.
//
// Each entry has a gpio number and an optional note. The note is returned by
// get_gpio_capabilities so the LLM (and developer) knows if a pin is wired
// to something onboard (e.g. an LED, button, or sensor). Leave note as NULL
// for a completely free header pin.
//
// Waveshare ESP32-C6-LCD-1.47 free pins (confirmed via CircuitPython pins.c):
//   GPIO 0,1,2,3  — ADC1-capable (CH0–CH3), physically on header
//   GPIO 18,19,20,23 — digital only, physically on header
//
// *** CHANGE THIS LIST for your board ***
// =============================================================================

typedef struct {
    int         gpio;
    const char *note;   // NULL = bare header pin; or brief description of onboard connection
} safe_pin_info_t;

static const safe_pin_info_t SAFE_DIGITAL_PINS[] = {
    { 0,  NULL },
    { 1,  NULL },
    { 2,  NULL },
    { 3,  NULL },
    { 18, NULL },
    { 19, NULL },
    { 20, NULL },
    { 23, NULL },
};
#define SAFE_DIGITAL_COUNT  8   // must match the array length above

// ── Onboard addressable RGB LED ────────────────────────────────────────────
// Set to -1 if your board has no addressable LED.
// This pin is NOT in SAFE_DIGITAL_PINS (it requires RMT, not simple GPIO),
// but is exposed via the dedicated set_rgb_led MCP tool.
//
// *** CHANGE THIS for your board ***
#define BOARD_RGB_LED_GPIO  8   // WS2812 on GPIO 8 (RMT, single pixel)

// =============================================================================
// SECTION 3 — ADC-Capable Pins
//
// Subset of SAFE_DIGITAL_PINS that can be configured in ADC mode.
// Must be valid ADC1 channel pins on your specific ESP32 variant.
//
// ESP32-C6 ADC1 channel map:
//   GPIO 0 → ADC1_CHANNEL_0
//   GPIO 1 → ADC1_CHANNEL_1
//   GPIO 2 → ADC1_CHANNEL_2
//   GPIO 3 → ADC1_CHANNEL_3
//   GPIO 4 → ADC1_CHANNEL_4  (SD card CS on this board — excluded)
//   GPIO 5 → ADC1_CHANNEL_5  (SD card MISO on this board — excluded)
//   GPIO 6 → ADC1_CHANNEL_6  (SPI MOSI — excluded)
//
// *** CHANGE THIS LIST for your board ***
// =============================================================================

static const int ADC_CAPABLE_PINS[] = { 0, 1, 2, 3 };
#define ADC_CAPABLE_COUNT   4   // must match the array length above

// ADC1 channel numbers corresponding 1:1 to ADC_CAPABLE_PINS[].
// Each entry maps ADC_CAPABLE_PINS[i] → ADC1 channel for adc_oneshot API.
//
// *** CHANGE THESE to match your ADC_CAPABLE_PINS entries ***
static const adc_channel_t ADC_CHANNELS[] = {
    ADC_CHANNEL_0,   // GPIO 0
    ADC_CHANNEL_1,   // GPIO 1
    ADC_CHANNEL_2,   // GPIO 2
    ADC_CHANNEL_3,   // GPIO 3
};

// Attenuation sets the measurable voltage range on all ADC pins.
// Pick the option that covers the voltages your sensors will output:
//   ADC_ATTEN_DB_0   → 0 – 0.8 V
//   ADC_ATTEN_DB_2_5 → 0 – 1.1 V
//   ADC_ATTEN_DB_6   → 0 – 1.35 V (was 2.2V, hardware-revised on C6)
//   ADC_ATTEN_DB_12  → 0 – 3.1 V  (best for 3.3V-powered sensors)
//
// *** CHANGE THIS if your sensor range differs ***
#define ADC_ATTENUATION  ADC_ATTEN_DB_12

// ADC result is in millivolts. Raw 12-bit counts are converted internally
// via adc_cali_raw_to_voltage(); the read_pins tool always returns mV.

// =============================================================================
// SECTION 4 — Reserved Pin Descriptions
//
// This table is returned verbatim by the get_gpio_capabilities tool.
// Its purpose is to give the LLM an accurate mental model of why certain
// GPIO numbers cannot be configured — preventing hallucinated pin requests.
//
// Format: { gpio_number, "human-readable reason" }
//
// Include every GPIO that is wired to an onboard peripheral, absent from the
// header, or otherwise unsafe to touch. You do NOT need to list every possible
// ESP32 pin — only the ones an LLM might reasonably try to use.
//
// *** CHANGE THESE entries to match your board ***
// =============================================================================

typedef struct {
    int         gpio;
    const char *reason;
} reserved_pin_info_t;

static const reserved_pin_info_t g_reserved_pins[] = {
    // SPI2 bus — shared by LCD and SD card
    { 6,  "SPI2 MOSI (shared LCD + SD bus)" },
    { 7,  "SPI2 SCLK (shared LCD + SD bus)" },

    // SD card (SPI2 slave)
    { 4,  "SD card chip select (SPI2)" },
    { 5,  "SD card MISO (SPI2)" },

    // LCD ST7789 control lines
    { 14, "LCD chip select" },
    { 15, "LCD data/command (strapping pin)" },
    { 21, "LCD reset" },
    { 22, "LCD backlight PWM (LEDC)" },

    // Onboard LED
    { 8,  "WS2812 RGB LED (RMT) / strapping pin" },

    // Onboard button
    { 9,  "BOOT button (strapping pin, pull-up, LOW = pressed)" },

    // USB — chip-internal, not on header
    { 12, "USB D- (not on header)" },
    { 13, "USB D+ (not on header)" },

    // UART bridge — excluded to preserve serial monitor
    { 16, "UART0 TX (serial monitor)" },
    { 17, "UART0 RX (serial monitor)" },
};

#define RESERVED_PINS_COUNT  (sizeof(g_reserved_pins) / sizeof(g_reserved_pins[0]))

// =============================================================================
// SECTION 5 — Derived Validation Helpers
//
// These inline helpers are used by the tool handlers to validate LLM requests
// against the tables above. Do not modify these — they read from the tables.
// =============================================================================

#include <stdbool.h>

// Returns true if gpio is in SAFE_DIGITAL_PINS[].
static inline bool board_is_safe_pin(int gpio)
{
    for (int i = 0; i < SAFE_DIGITAL_COUNT; i++) {
        if (SAFE_DIGITAL_PINS[i].gpio == gpio) return true;
    }
    return false;
}

// Returns true if gpio is in ADC_CAPABLE_PINS[].
static inline bool board_is_adc_pin(int gpio)
{
    for (int i = 0; i < ADC_CAPABLE_COUNT; i++) {
        if (ADC_CAPABLE_PINS[i] == gpio) return true;
    }
    return false;
}

// Returns the note string for a safe pin, or NULL if none.
static inline const char *board_pin_note(int gpio)
{
    for (int i = 0; i < SAFE_DIGITAL_COUNT; i++) {
        if (SAFE_DIGITAL_PINS[i].gpio == gpio) return SAFE_DIGITAL_PINS[i].note;
    }
    return NULL;
}

// Returns the ADC1 channel for a given gpio, or -1 if not ADC-capable.
static inline int board_adc_channel(int gpio)
{
    for (int i = 0; i < ADC_CAPABLE_COUNT; i++) {
        if (ADC_CAPABLE_PINS[i] == gpio) return (int)ADC_CHANNELS[i];
    }
    return -1;
}
