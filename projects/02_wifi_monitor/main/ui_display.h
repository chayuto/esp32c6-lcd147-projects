#pragma once

#include "app_state.h"

// Create all LVGL objects. Call once from the LVGL task context.
void ui_display_init(void);

// Refresh all visible elements. Must be called from an lv_timer callback.
void ui_display_update(const analyzer_metrics_t *m);

// Highlight the active channel label (1, 6, or 11).
void ui_display_set_channel(uint8_t channel);
