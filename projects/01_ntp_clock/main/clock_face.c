#include "clock_face.h"
#include "theme.h"
#include "led_ctrl.h"

#include "lvgl.h"
#include <string.h>
#include <stdio.h>

// Screen: 172 x 320 portrait
// Arc center: x=86, y=155. Outer radius ~68px.
#define ARC_CENTER_X   86
#define ARC_CENTER_Y   155
#define ARC_SIZE       136  // diameter

static lv_obj_t *s_screen;
static lv_obj_t *s_arc_bg;
static lv_obj_t *s_arc_sec;
static lv_obj_t *s_label_hh;
static lv_obj_t *s_label_colon;
static lv_obj_t *s_label_mm;
static lv_obj_t *s_label_sec;
static lv_obj_t *s_label_day;
static lv_obj_t *s_label_date;
static lv_obj_t *s_label_year;
static lv_obj_t *s_label_wifi;
static lv_obj_t *s_label_ntp;

static int  s_last_min = -1;
static int  s_last_hour = -1;
static lv_anim_t s_colon_anim;

static const char *day_names[] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};
static const char *month_names[] = {
    "","Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

// --- Colon blink animation ---
static void colon_opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void start_colon_anim(void)
{
    lv_anim_init(&s_colon_anim);
    lv_anim_set_var(&s_colon_anim, s_label_colon);
    lv_anim_set_exec_cb(&s_colon_anim, colon_opa_anim_cb);
    lv_anim_set_values(&s_colon_anim, LV_OPA_COVER, 80);
    lv_anim_set_time(&s_colon_anim, 1000);
    lv_anim_set_playback_time(&s_colon_anim, 1000);
    lv_anim_set_path_cb(&s_colon_anim, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&s_colon_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_colon_anim);
}

// --- Arc seconds animation ---
static void arc_angle_anim_cb(void *obj, int32_t v)
{
    lv_arc_set_end_angle((lv_obj_t *)obj, (uint16_t)v);
}

static void animate_arc_to_second(int sec)
{
    // Map 0-59 seconds to 270-629 degrees (top = 270, clockwise full circle)
    int16_t start_angle = 270 + ((sec)     * 360 / 60);
    int16_t end_angle   = 270 + ((sec + 1) * 360 / 60);

    if (sec == 0) {
        // Reset arc instantly then animate forward
        lv_arc_set_start_angle(s_arc_sec, 270);
        lv_arc_set_end_angle(s_arc_sec, 270);
        // Flash white briefly
        lv_obj_set_style_arc_color(s_arc_sec, lv_color_white(), LV_PART_INDICATOR);
        lv_arc_set_end_angle(s_arc_sec, end_angle);
        // Restore theme color after short delay via a one-shot timer
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_arc_sec);
    lv_anim_set_exec_cb(&a, arc_angle_anim_cb);
    lv_anim_set_values(&a, start_angle, end_angle);
    lv_anim_set_time(&a, 900);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// --- Time label fade on digit change ---
static void label_opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void flash_time_label(lv_obj_t *label)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, label_opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 150);
    lv_anim_set_playback_time(&a, 250);
    lv_anim_set_playback_delay(&a, 0);
    lv_anim_set_repeat_count(&a, 1);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void clock_face_init(void)
{
    const theme_t *t = theme_current();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, t->bg_color, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // --- Status bar ---
    s_label_wifi = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_label_wifi, lv_color_make(0x2E, 0xCC, 0x71), 0);
    lv_label_set_text(s_label_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(s_label_wifi, LV_ALIGN_TOP_LEFT, 8, 6);

    s_label_ntp = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_ntp, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_label_ntp, lv_color_make(0xF3, 0x9C, 0x12), 0);
    lv_label_set_text(s_label_ntp, LV_SYMBOL_REFRESH);
    lv_obj_align(s_label_ntp, LV_ALIGN_TOP_RIGHT, -8, 6);

    // --- Arc background ring ---
    s_arc_bg = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc_bg, ARC_SIZE, ARC_SIZE);
    lv_obj_align(s_arc_bg, LV_ALIGN_TOP_MID, 0, 38);
    lv_arc_set_bg_angles(s_arc_bg, 0, 360);
    lv_arc_set_value(s_arc_bg, 0);
    lv_obj_set_style_arc_color(s_arc_bg, lv_color_make(0x1E, 0x2D, 0x3D), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_bg, lv_color_make(0x1E, 0x2D, 0x3D), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_bg, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_bg, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(s_arc_bg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_bg, LV_OBJ_FLAG_CLICKABLE);

    // --- Seconds arc indicator ---
    s_arc_sec = lv_arc_create(s_screen);
    lv_obj_set_size(s_arc_sec, ARC_SIZE, ARC_SIZE);
    lv_obj_align(s_arc_sec, LV_ALIGN_TOP_MID, 0, 38);
    lv_arc_set_start_angle(s_arc_sec, 270);
    lv_arc_set_end_angle(s_arc_sec, 270);
    lv_obj_set_style_arc_color(s_arc_sec, t->arc_color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_sec, lv_color_make(0x00, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_arc_sec, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_sec, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(s_arc_sec, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_sec, LV_OBJ_FLAG_CLICKABLE);

    // --- Time labels (HH : MM) ---
    int time_y = 38 + ARC_SIZE / 2 - 22; // vertically centered in arc

    s_label_hh = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_hh, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_label_hh, t->text_color, 0);
    lv_label_set_text(s_label_hh, "00");
    lv_obj_align(s_label_hh, LV_ALIGN_TOP_MID, -28, time_y);

    s_label_colon = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_colon, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_label_colon, t->arc_color, 0);
    lv_label_set_text(s_label_colon, ":");
    lv_obj_align(s_label_colon, LV_ALIGN_TOP_MID, 0, time_y);

    s_label_mm = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_mm, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_label_mm, t->text_color, 0);
    lv_label_set_text(s_label_mm, "00");
    lv_obj_align(s_label_mm, LV_ALIGN_TOP_MID, 28, time_y);

    // --- Seconds label ---
    s_label_sec = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_sec, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_label_sec, t->dim_color, 0);
    lv_label_set_text(s_label_sec, ":00");
    lv_obj_align(s_label_sec, LV_ALIGN_TOP_MID, 0, time_y + 42);

    // --- Date labels ---
    s_label_day = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_day, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_label_day, t->dim_color, 0);
    lv_label_set_text(s_label_day, "Thursday");
    lv_obj_align(s_label_day, LV_ALIGN_TOP_MID, 0, 230);

    s_label_date = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_date, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_label_date, t->text_color, 0);
    lv_label_set_text(s_label_date, "01 Jan");
    lv_obj_align(s_label_date, LV_ALIGN_TOP_MID, 0, 254);

    s_label_year = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_label_year, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_label_year, t->dim_color, 0);
    lv_label_set_text(s_label_year, "2026");
    lv_obj_align(s_label_year, LV_ALIGN_TOP_MID, 0, 280);

    start_colon_anim();
    lv_scr_load(s_screen);
}

void clock_face_update(const struct tm *t)
{
    if (!s_screen) return;

    char buf[16];

    // Seconds arc
    animate_arc_to_second(t->tm_sec);

    // Restore arc theme color if it was white-flashed (minute == 0)
    if (t->tm_sec == 0) {
        lv_obj_set_style_arc_color(s_arc_sec, theme_current()->arc_color, LV_PART_INDICATOR);
        led_ctrl_flash_white();
    }

    // Second label
    snprintf(buf, sizeof(buf), ":%02d", t->tm_sec);
    lv_label_set_text(s_label_sec, buf);

    // Minutes — animate on change
    if (t->tm_min != s_last_min) {
        snprintf(buf, sizeof(buf), "%02d", t->tm_min);
        flash_time_label(s_label_mm);
        lv_label_set_text(s_label_mm, buf);
        s_last_min = t->tm_min;
    }

    // Hours — animate on change
    if (t->tm_hour != s_last_hour) {
        snprintf(buf, sizeof(buf), "%02d", t->tm_hour);
        flash_time_label(s_label_hh);
        lv_label_set_text(s_label_hh, buf);
        s_last_hour = t->tm_hour;
    }

    // Date (only needs to update once, but cheap to set every second)
    snprintf(buf, sizeof(buf), "%02d %s", t->tm_mday, month_names[t->tm_mon + 1]);
    lv_label_set_text(s_label_date, buf);
    lv_label_set_text(s_label_day, day_names[t->tm_wday]);
    snprintf(buf, sizeof(buf), "%04d", t->tm_year + 1900);
    lv_label_set_text(s_label_year, buf);

    // LED second pulse
    led_ctrl_pulse_once();
}

void clock_face_apply_theme(void)
{
    if (!s_screen) return;
    const theme_t *t = theme_current();

    lv_obj_set_style_bg_color(s_screen, t->bg_color, 0);
    lv_obj_set_style_arc_color(s_arc_sec, t->arc_color, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_label_colon, t->arc_color, 0);
    lv_obj_set_style_text_color(s_label_hh, t->text_color, 0);
    lv_obj_set_style_text_color(s_label_mm, t->text_color, 0);
    lv_obj_set_style_text_color(s_label_sec, t->dim_color, 0);
    lv_obj_set_style_text_color(s_label_day, t->dim_color, 0);
    lv_obj_set_style_text_color(s_label_date, t->text_color, 0);
    lv_obj_set_style_text_color(s_label_year, t->dim_color, 0);

    led_ctrl_set_theme();
}

void clock_face_set_wifi_status(bool connected)
{
    if (!s_label_wifi) return;
    lv_obj_set_style_text_color(
        s_label_wifi,
        connected ? lv_color_make(0x2E, 0xCC, 0x71) : lv_color_make(0xE7, 0x4C, 0x3C),
        0
    );
}

void clock_face_set_ntp_status(bool synced)
{
    if (!s_label_ntp) return;
    lv_obj_set_style_text_color(
        s_label_ntp,
        synced ? lv_color_make(0x00, 0xB4, 0xD8) : lv_color_make(0xF3, 0x9C, 0x12),
        0
    );
}
