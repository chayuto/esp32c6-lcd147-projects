#pragma once

#include <stdint.h>
#include <stdbool.h>

void led_ctrl_init(void);
void led_ctrl_set(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_ms);
void led_ctrl_set_theme(void);       // apply current theme ambient color with fade
void led_ctrl_pulse_once(void);      // second-tick pulse
void led_ctrl_flash_white(void);     // minute-change flash
void led_ctrl_boot_breathing(void);  // slow white breathing (connecting state)
void led_ctrl_ntp_flash(void);       // green flash on sync
void led_ctrl_error_pulse(void);     // red pulsing (error state)
void led_ctrl_set_enabled(bool en);  // long-press toggle
