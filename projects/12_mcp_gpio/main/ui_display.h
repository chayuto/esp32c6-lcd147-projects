#pragma once

// Create LVGL widgets (title label, IP label, pin-state table).
// Must be called after LVGL_Init() and before the LVGL task starts.
void ui_display_init(void);

// Show "Connecting to Wi-Fi..." in the IP label while wi-fi is pending.
void ui_display_show_connecting(void);

// Replace IP label with the assigned address and start the 500 ms refresh timer.
void ui_display_show_ready(const char *ip);
