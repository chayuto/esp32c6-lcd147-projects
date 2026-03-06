#include "ui_display.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdint.h>

// Object handles
static lv_obj_t *s_lbl_ch[3]    = {NULL};
static lv_obj_t *s_bar_cu        = NULL;
static lv_obj_t *s_lbl_cu_pct   = NULL;
static lv_obj_t *s_bar_rf        = NULL;
static lv_obj_t *s_lbl_rf_score  = NULL;
static lv_obj_t *s_lbl_snr       = NULL;
static lv_obj_t *s_lbl_noise     = NULL;
static lv_obj_t *s_lbl_dev       = NULL;
static lv_obj_t *s_lbl_pkt       = NULL;
static lv_obj_t *s_lbl_drop      = NULL;

static const char   *k_ch_labels[] = {"CH1", "CH6", "CH11"};
static const uint8_t k_channels[]  = {1, 6, 11};

// Helper: create a muted section title label
static lv_obj_t *make_title(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0x6C, 0x75, 0x7D), 0);
    return lbl;
}

// Helper: create a value label (white, montserrat-14)
static lv_obj_t *make_value(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xAD, 0xB5, 0xBD), 0);
    return lbl;
}

void ui_display_init(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x0D, 0x11, 0x17), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // --- Channel selector row (y=0..28) ---
    lv_obj_t *ch_row = lv_obj_create(scr);
    lv_obj_set_size(ch_row, 172, 28);
    lv_obj_set_pos(ch_row, 0, 0);
    lv_obj_set_style_bg_color(ch_row, lv_color_make(0x1E, 0x2D, 0x3D), 0);
    lv_obj_set_style_bg_opa(ch_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch_row, 0, 0);
    lv_obj_set_style_pad_all(ch_row, 0, 0);
    lv_obj_clear_flag(ch_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        s_lbl_ch[i] = lv_label_create(ch_row);
        lv_label_set_text(s_lbl_ch[i], k_ch_labels[i]);
        lv_obj_set_pos(s_lbl_ch[i], 12 + i * 54, 7);
        lv_obj_set_style_text_font(s_lbl_ch[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_lbl_ch[i], lv_color_make(0x2D, 0x37, 0x48), 0);
    }
    // Highlight channel 6 (index 1) by default
    lv_obj_set_style_text_color(s_lbl_ch[1], lv_color_make(0x00, 0xB4, 0xD8), 0);

    // --- Channel utilization (y=36..86) ---
    make_title(scr, "CHANNEL USE", 8, 36);

    s_bar_cu = lv_bar_create(scr);
    lv_obj_set_size(s_bar_cu, 130, 16);
    lv_obj_set_pos(s_bar_cu, 8, 56);
    lv_bar_set_range(s_bar_cu, 0, 100);
    lv_bar_set_value(s_bar_cu, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_cu, lv_color_make(0x1E, 0x2D, 0x3D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_cu, lv_color_make(0x2E, 0xCC, 0x71), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_cu, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_cu, 4, LV_PART_INDICATOR);

    s_lbl_cu_pct = lv_label_create(scr);
    lv_label_set_text(s_lbl_cu_pct, " 0%");
    lv_obj_set_pos(s_lbl_cu_pct, 140, 57);
    lv_obj_set_style_text_font(s_lbl_cu_pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_cu_pct, lv_color_make(0xFF, 0xFF, 0xFF), 0);

    // --- RF Quality (y=88..138) ---
    make_title(scr, "RF QUALITY", 8, 88);

    s_bar_rf = lv_bar_create(scr);
    lv_obj_set_size(s_bar_rf, 130, 16);
    lv_obj_set_pos(s_bar_rf, 8, 108);
    lv_bar_set_range(s_bar_rf, 0, 100);
    lv_bar_set_value(s_bar_rf, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_rf, lv_color_make(0x1E, 0x2D, 0x3D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_rf, lv_color_make(0x00, 0xB4, 0xD8), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_rf, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_rf, 4, LV_PART_INDICATOR);

    s_lbl_rf_score = lv_label_create(scr);
    lv_label_set_text(s_lbl_rf_score, "--");
    lv_obj_set_pos(s_lbl_rf_score, 140, 109);
    lv_obj_set_style_text_font(s_lbl_rf_score, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_rf_score, lv_color_make(0xFF, 0xFF, 0xFF), 0);

    // --- SNR / Noise (y=148..180) ---
    s_lbl_snr   = make_value(scr, "AVG SNR  -- dB",  8, 148);
    s_lbl_noise = make_value(scr, "NOISE  -- dBm",   8, 168);

    // --- Devices (y=196..268) ---
    make_title(scr, "DEVICES NEARBY", 8, 196);

    s_lbl_dev = lv_label_create(scr);
    lv_label_set_text(s_lbl_dev, "0");
    lv_obj_set_style_text_font(s_lbl_dev, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_dev, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_width(s_lbl_dev, 172);
    lv_obj_set_pos(s_lbl_dev, 0, 218);
    lv_obj_set_style_text_align(s_lbl_dev, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *lbl_est = lv_label_create(scr);
    lv_label_set_text(lbl_est, "(estimated)");
    lv_obj_set_width(lbl_est, 172);
    lv_obj_set_pos(lbl_est, 0, 258);
    lv_obj_set_style_text_align(lbl_est, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_est, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_est, lv_color_make(0x6C, 0x75, 0x7D), 0);

    // --- PKT/S and DROP (y=282..306) ---
    s_lbl_pkt  = make_value(scr, "PKT/S  --",  8, 282);
    s_lbl_drop = make_value(scr, "DROP   --%", 8, 300);

    lv_scr_load(scr);
}

void ui_display_update(const analyzer_metrics_t *m)
{
    char buf[40];
    int cu_int = (int)m->cu_pct;

    // CU bar + color
    lv_bar_set_value(s_bar_cu, cu_int, LV_ANIM_ON);

    lv_color_t cu_col;
    if (cu_int < 40) {
        cu_col = lv_color_make(0x2E, 0xCC, 0x71); // green
    } else if (cu_int < 70) {
        cu_col = lv_color_make(0xF3, 0x9C, 0x12); // orange
    } else {
        cu_col = lv_color_make(0xE7, 0x4C, 0x3C); // red
    }
    lv_obj_set_style_bg_color(s_bar_cu, cu_col, LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "%d%%", cu_int);
    lv_label_set_text(s_lbl_cu_pct, buf);

    // RF quality bar
    int rf_int = (int)m->rf_score;
    lv_bar_set_value(s_bar_rf, rf_int, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d", rf_int);
    lv_label_set_text(s_lbl_rf_score, buf);

    // SNR / noise
    snprintf(buf, sizeof(buf), "AVG SNR  %.0f dB", m->avg_snr_db);
    lv_label_set_text(s_lbl_snr, buf);

    snprintf(buf, sizeof(buf), "NOISE  %d dBm", (int)m->avg_noise_floor);
    lv_label_set_text(s_lbl_noise, buf);

    // Device count — apply 0.7x correction factor for overcounting
    uint32_t display_count = (uint32_t)((double)m->device_count * 0.7);
    if (display_count == 0 && m->device_count > 0) display_count = 1;
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)display_count);
    lv_label_set_text(s_lbl_dev, buf);

    // PKT/S + DROP
    snprintf(buf, sizeof(buf), "PKT/S  %lu", (unsigned long)m->pkt_per_sec);
    lv_label_set_text(s_lbl_pkt, buf);

    snprintf(buf, sizeof(buf), "DROP   %d%%", m->drop_pct);
    lv_label_set_text(s_lbl_drop, buf);

    ui_display_set_channel(m->active_channel);
}

void ui_display_set_channel(uint8_t channel)
{
    for (int i = 0; i < 3; i++) {
        bool active = (k_channels[i] == channel);
        lv_obj_set_style_text_color(s_lbl_ch[i],
            active ? lv_color_make(0x00, 0xB4, 0xD8)
                   : lv_color_make(0x2D, 0x37, 0x48),
            0);
    }
}
