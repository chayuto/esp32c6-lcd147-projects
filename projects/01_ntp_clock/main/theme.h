#pragma once

#include "lvgl.h"
#include <stdint.h>

typedef struct {
    const char *name;
    lv_color_t  arc_color;
    lv_color_t  text_color;
    lv_color_t  dim_color;
    lv_color_t  bg_color;
    uint8_t     led_r;
    uint8_t     led_g;
    uint8_t     led_b;
} theme_t;

extern const theme_t themes[];
extern const int     theme_count;

void theme_apply(int index);
int  theme_current_index(void);
const theme_t *theme_current(void);
