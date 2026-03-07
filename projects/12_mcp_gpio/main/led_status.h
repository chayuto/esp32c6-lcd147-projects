#pragma once

// WS2812 RGB LED (GPIO 8) connection-state indicator.
//
// Call order:
//   led_status_init()        — once, before LVGL task starts
//   led_status_connecting()  — while Wi-Fi is associating (blinks amber)
//   led_status_ready()       — after MCP server starts (solid green)
//   led_status_error()       — if Wi-Fi fails (solid red)
//   led_status_set_rgb()     — MCP tool override, any colour (0-255 each channel)

#include <stdint.h>

void led_status_init(void);
void led_status_connecting(void);
void led_status_ready(void);
void led_status_error(void);
void led_status_set_rgb(uint8_t r, uint8_t g, uint8_t b);
