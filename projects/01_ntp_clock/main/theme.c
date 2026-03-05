#include "theme.h"

const theme_t themes[] = {
    {
        .name       = "Cyan",
        .arc_color  = LV_COLOR_MAKE(0x00, 0xB4, 0xD8),
        .text_color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
        .dim_color  = LV_COLOR_MAKE(0x8D, 0x99, 0xAE),
        .bg_color   = LV_COLOR_MAKE(0x0D, 0x11, 0x17),
        .led_r = 0, .led_g = 45, .led_b = 55,
    },
    {
        .name       = "Amber",
        .arc_color  = LV_COLOR_MAKE(0xF3, 0x9C, 0x12),
        .text_color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
        .dim_color  = LV_COLOR_MAKE(0x8D, 0x99, 0xAE),
        .bg_color   = LV_COLOR_MAKE(0x0D, 0x11, 0x17),
        .led_r = 60, .led_g = 39, .led_b = 5,
    },
    {
        .name       = "Green",
        .arc_color  = LV_COLOR_MAKE(0x2E, 0xCC, 0x71),
        .text_color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
        .dim_color  = LV_COLOR_MAKE(0x8D, 0x99, 0xAE),
        .bg_color   = LV_COLOR_MAKE(0x0D, 0x11, 0x17),
        .led_r = 0, .led_g = 51, .led_b = 28,
    },
    {
        .name       = "Purple",
        .arc_color  = LV_COLOR_MAKE(0x9B, 0x59, 0xB6),
        .text_color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
        .dim_color  = LV_COLOR_MAKE(0x8D, 0x99, 0xAE),
        .bg_color   = LV_COLOR_MAKE(0x0D, 0x11, 0x17),
        .led_r = 38, .led_g = 22, .led_b = 45,
    },
};

const int theme_count = sizeof(themes) / sizeof(themes[0]);

static int s_current = 0;

void theme_apply(int index)
{
    s_current = index % theme_count;
}

int theme_current_index(void) { return s_current; }

const theme_t *theme_current(void) { return &themes[s_current]; }
