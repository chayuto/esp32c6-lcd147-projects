#include "ui_display.h"
#include "drawing_engine.h"
#include "esp_log.h"

static const char *TAG = "ui";

// Full-screen canvas pixel buffer in BSS — 110,080 bytes, never heap-allocated
lv_color_t        g_canvas_buf[SCREEN_W * SCREEN_H];
SemaphoreHandle_t g_canvas_mutex;

static lv_obj_t  *s_canvas = NULL;

void ui_display_init(void)
{
    g_canvas_mutex = xSemaphoreCreateMutex();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, g_canvas_buf, SCREEN_W, SCREEN_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(s_canvas, lv_color_make(0, 0, 0), LV_OPA_COVER);
    lv_obj_set_pos(s_canvas, 0, 0);

    lv_scr_load(scr);
    ESP_LOGI(TAG, "Canvas ready (%dx%d, %u bytes)", SCREEN_W, SCREEN_H,
             (unsigned)sizeof(g_canvas_buf));
}

void ui_display_show_connecting(void)
{
    draw_cmd_t cmd = { .type = CMD_CLEAR, .clear_color = {0, 0, 0} };
    xQueueSend(g_draw_queue, &cmd, portMAX_DELAY);
    drawing_push_text(4, 140, 0, 180, 100, 14, "Connecting to Wi-Fi...");
}

void ui_display_show_ip(const char *ip_str)
{
    char line[48];
    drawing_push_clear(0, 0, 0);
    snprintf(line, sizeof(line), "IP: %s", ip_str);
    drawing_push_text(4, 130, 0, 220, 120, 14, line);
    drawing_push_text(4, 152, 60, 60, 60, 14, "POST /mcp");
    drawing_push_text(4, 170, 60, 60, 60, 14, "esp32-canvas.local");
    drawing_push_text(4, 195, 0, 160, 80, 14, "MCP Ready");
}

// Called from lvgl_task every 50 ms — drains queue, renders to canvas
void ui_render_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_canvas) return;

    draw_cmd_t cmd;
    while (xQueueReceive(g_draw_queue, &cmd, 0) == pdTRUE) {
        if (xSemaphoreTake(g_canvas_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            // Canvas locked by snapshot — requeue and try next tick
            xQueueSendToFront(g_draw_queue, &cmd, 0);
            break;
        }

        switch (cmd.type) {
            case CMD_CLEAR: {
                lv_canvas_fill_bg(s_canvas,
                    lv_color_make(cmd.clear_color.r,
                                  cmd.clear_color.g,
                                  cmd.clear_color.b),
                    LV_OPA_COVER);
                break;
            }
            case CMD_DRAW_RECT: {
                lv_draw_rect_dsc_t dsc;
                lv_draw_rect_dsc_init(&dsc);
                dsc.radius = cmd.rect.radius;
                if (cmd.rect.filled) {
                    dsc.bg_color   = lv_color_make(cmd.rect.color.r,
                                                   cmd.rect.color.g,
                                                   cmd.rect.color.b);
                    dsc.bg_opa     = LV_OPA_COVER;
                    dsc.border_width = 0;
                } else {
                    dsc.bg_opa       = LV_OPA_TRANSP;
                    dsc.border_color = lv_color_make(cmd.rect.color.r,
                                                     cmd.rect.color.g,
                                                     cmd.rect.color.b);
                    dsc.border_width = 1;
                    dsc.border_opa   = LV_OPA_COVER;
                }
                lv_canvas_draw_rect(s_canvas,
                    cmd.rect.x, cmd.rect.y,
                    cmd.rect.w, cmd.rect.h, &dsc);
                break;
            }
            case CMD_DRAW_LINE: {
                lv_point_t pts[2] = {
                    {cmd.line.x1, cmd.line.y1},
                    {cmd.line.x2, cmd.line.y2},
                };
                lv_draw_line_dsc_t dsc;
                lv_draw_line_dsc_init(&dsc);
                dsc.color = lv_color_make(cmd.line.color.r,
                                          cmd.line.color.g,
                                          cmd.line.color.b);
                dsc.width = cmd.line.width;
                lv_canvas_draw_line(s_canvas, pts, 2, &dsc);
                break;
            }
            case CMD_DRAW_ARC: {
                lv_draw_arc_dsc_t dsc;
                lv_draw_arc_dsc_init(&dsc);
                dsc.color = lv_color_make(cmd.arc.color.r,
                                          cmd.arc.color.g,
                                          cmd.arc.color.b);
                dsc.width = cmd.arc.width;
                lv_canvas_draw_arc(s_canvas,
                    cmd.arc.cx, cmd.arc.cy, cmd.arc.radius,
                    cmd.arc.start_angle, cmd.arc.end_angle, &dsc);
                break;
            }
            case CMD_DRAW_TEXT: {
                lv_draw_label_dsc_t dsc;
                lv_draw_label_dsc_init(&dsc);
                dsc.color = lv_color_make(cmd.text.color.r,
                                          cmd.text.color.g,
                                          cmd.text.color.b);
                dsc.font  = (cmd.text.font_size == 20)
                            ? &lv_font_montserrat_20
                            : &lv_font_montserrat_14;
                lv_coord_t max_w = SCREEN_W - cmd.text.x;
                if (max_w <= 0) max_w = SCREEN_W;
                lv_canvas_draw_text(s_canvas,
                    cmd.text.x, cmd.text.y, max_w, &dsc, cmd.text.text);
                break;
            }
            case CMD_DRAW_PATH: {
                if (!cmd.path.closed) {
                    lv_draw_line_dsc_t dsc;
                    lv_draw_line_dsc_init(&dsc);
                    dsc.color = lv_color_make(cmd.path.color.r,
                                              cmd.path.color.g,
                                              cmd.path.color.b);
                    dsc.width = cmd.path.width;
                    lv_canvas_draw_line(s_canvas,
                        cmd.path.pts, cmd.path.pt_cnt, &dsc);
                } else {
                    lv_draw_rect_dsc_t dsc;
                    lv_draw_rect_dsc_init(&dsc);
                    if (cmd.path.filled) {
                        dsc.bg_color   = lv_color_make(cmd.path.color.r,
                                                       cmd.path.color.g,
                                                       cmd.path.color.b);
                        dsc.bg_opa     = LV_OPA_COVER;
                        dsc.border_width = 0;
                    } else {
                        dsc.bg_opa       = LV_OPA_TRANSP;
                        dsc.border_color = lv_color_make(cmd.path.color.r,
                                                         cmd.path.color.g,
                                                         cmd.path.color.b);
                        dsc.border_width = cmd.path.width;
                        dsc.border_opa   = LV_OPA_COVER;
                    }
                    lv_canvas_draw_polygon(s_canvas,
                        cmd.path.pts, cmd.path.pt_cnt, &dsc);
                }
                break;
            }
        }

        xSemaphoreGive(g_canvas_mutex);
    }
}
