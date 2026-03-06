#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "drawing_engine.h"

// Canvas pixel buffer — declared here so snapshot.c can access it directly
extern lv_color_t        g_canvas_buf[SCREEN_W * SCREEN_H];
// Mutex protecting g_canvas_buf between render timer (writer) and snapshot (reader)
extern SemaphoreHandle_t g_canvas_mutex;

void ui_display_init(void);
void ui_display_show_connecting(void);
void ui_display_show_ip(const char *ip_str);
void ui_render_timer_cb(lv_timer_t *timer);   // registered by lvgl_task in main.c
