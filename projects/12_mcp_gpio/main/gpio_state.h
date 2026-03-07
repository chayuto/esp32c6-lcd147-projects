#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Runtime mode of a single GPIO pin.
typedef enum {
    PIN_MODE_UNCONFIGURED = 0,
    PIN_MODE_INPUT,
    PIN_MODE_OUTPUT,
    PIN_MODE_ADC,
    PIN_MODE_PWM,
} pin_mode_t;

// Current state of a single GPIO pin.
// value meaning:
//   UNCONFIGURED → -1
//   INPUT/OUTPUT → 0 or 1
//   ADC          → millivolts (0–3100 with 12dB atten on 3.3V board)
//   PWM          → duty cycle 0–100 (%)
typedef struct {
    int        gpio;
    pin_mode_t mode;
    int        value;
} pin_state_t;

// Initialise pin state table and ADC oneshot unit. Call once before any other function.
esp_err_t    gpio_state_init(void);

// Configure a pin. gpio must be in SAFE_DIGITAL_PINS[]. ADC mode requires ADC_CAPABLE_PINS[].
esp_err_t    gpio_state_configure(int gpio, pin_mode_t mode);

// Set output level. Pin must already be configured as PIN_MODE_OUTPUT.
// Returns ESP_ERR_INVALID_STATE if pin is not OUTPUT.
esp_err_t    gpio_state_write(int gpio, int level);

// Read current hardware value and update state table.
// Returns ESP_ERR_INVALID_STATE if pin is UNCONFIGURED.
esp_err_t    gpio_state_read(int gpio, int *value_out);

// Copy current state to caller buffer (no hardware reads; just the last-known values).
// Used by mcp_tools.c to build get_gpio_capabilities responses.
void         gpio_state_snapshot(pin_state_t *out, int max_count, int *count_out);

// Read digital-level values for all INPUT/OUTPUT pins and copy state.
// ADC pins use last-known value (ADC reads are only triggered by read_pins tool).
// Called by the LVGL display timer for a fast, live dashboard update.
void         gpio_state_poll_digital(pin_state_t *out, int max_count, int *count_out);

// Set PWM duty cycle on a pin configured as PIN_MODE_PWM.
// duty_percent: 0 (always off) to 100 (always on).
// Returns ESP_ERR_INVALID_STATE if pin is not in PWM mode.
esp_err_t    gpio_state_set_pwm_duty(int gpio, int duty_percent);

// Returns a short uppercase string for a mode value ("---", "INPUT", "OUTPUT", "ADC", "PWM").
const char  *pin_mode_str(pin_mode_t mode);
