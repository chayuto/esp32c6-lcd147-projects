#include "boot_screen.h"
#include "lvgl.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_spinner;
static lv_obj_t *s_status;
static lv_obj_t *s_detail;

void boot_screen_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_make(0x0D, 0x11, 0x17), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Spinner arc centered
    s_spinner = lv_spinner_create(s_screen, 1000, 60);
    lv_obj_set_size(s_spinner, 80, 80);
    lv_obj_align(s_spinner, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_arc_color(s_spinner, lv_color_make(0x00, 0xB4, 0xD8), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, lv_color_make(0x1E, 0x2D, 0x3D), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_MAIN);

    // Status label
    s_status = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_status, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_status, "Connecting to WiFi...");
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 40);

    // Detail label (IP address / error)
    s_detail = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_detail, lv_color_make(0x8D, 0x99, 0xAE), 0);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_detail, "");
    lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 65);

    lv_scr_load(s_screen);
}

void boot_screen_update(app_state_t state, const char *detail)
{
    if (!s_screen) return;

    switch (state) {
    case APP_STATE_WIFI_CONNECTING:
        lv_label_set_text(s_status, "Connecting to WiFi...");
        lv_obj_set_style_arc_color(s_spinner, lv_color_make(0x00, 0xB4, 0xD8), LV_PART_INDICATOR);
        break;
    case APP_STATE_WIFI_ERROR:
        lv_label_set_text(s_status, "WiFi Error");
        lv_obj_set_style_arc_color(s_spinner, lv_color_make(0xE7, 0x4C, 0x3C), LV_PART_INDICATOR);
        break;
    case APP_STATE_NTP_SYNCING:
        lv_label_set_text(s_status, "Syncing time...");
        lv_obj_set_style_arc_color(s_spinner, lv_color_make(0xF3, 0x9C, 0x12), LV_PART_INDICATOR);
        break;
    case APP_STATE_NTP_ERROR:
        lv_label_set_text(s_status, "NTP Error");
        lv_obj_set_style_arc_color(s_spinner, lv_color_make(0xE7, 0x4C, 0x3C), LV_PART_INDICATOR);
        break;
    default:
        break;
    }

    if (detail && detail[0]) {
        lv_label_set_text(s_detail, detail);
    }
}

void boot_screen_destroy(void)
{
    if (s_screen) {
        lv_obj_del(s_screen);
        s_screen = NULL;
    }
}
