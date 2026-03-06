#pragma once

#include <stdint.h>
#include <stdbool.h>

// Initialise WS2812 LED on GPIO 8 via RMT.
void led_ctrl_init(void);

// Set LED color with optional crossfade. fade_ms=0 for instant.
void led_ctrl_set(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_ms);

// Set LED color based on channel utilization: green <40%, orange 40-69%, red >=70%.
void led_ctrl_set_cu(double cu_pct);

// Start slow white breathing (boot animation). Non-blocking.
void led_ctrl_boot_breathing(void);

// Enable or disable LED output.
void led_ctrl_set_enabled(bool en);
