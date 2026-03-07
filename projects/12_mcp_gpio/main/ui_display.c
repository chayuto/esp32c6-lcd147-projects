#include "ui_display.h"
#include "gpio_state.h"
#include "board_config.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

// ── Layout constants ──────────────────────────────────────────────────────
// Display: 172 × 320 portrait
//   Title label  : top,    ~28px
//   IP label     : below,  ~22px
//   Table        : remainder, 8 data rows + 1 header row

#define COL_GPIO_W   38    // "GPIO" + 2-char number
#define COL_MODE_W   88    // "OUTPUT" / "INPUT" / "ADC" / "---"
#define COL_VAL_W    46    // "HIGH" / "1823mV" / "--"
#define TABLE_COLS    3

// ── Widget handles ────────────────────────────────────────────────────────

static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_table;

// Snapshot buffer used by the display timer (avoids re-allocating each tick).
static pin_state_t s_snap[SAFE_DIGITAL_COUNT];
static int         s_snap_count;

// ── Color helpers ─────────────────────────────────────────────────────────
//
// Colors deliberately muted — readable on a bright LCD in a room:
//   OUTPUT HIGH  → dark green
//   OUTPUT LOW   → dark red
//   INPUT        → amber
//   ADC          → blue
//   UNCONFIGURED → default (no tint)

static lv_color_t row_bg_color(const pin_state_t *s)
{
    switch (s->mode) {
        case PIN_MODE_OUTPUT:
            return (s->value == 1) ? lv_color_make(0, 130, 0)
                                   : lv_color_make(150, 0, 0);
        case PIN_MODE_INPUT:
            return lv_color_make(160, 100, 0);
        case PIN_MODE_ADC:
            return lv_color_make(0, 80, 170);
        case PIN_MODE_PWM:
            return lv_color_make(100, 0, 160);  // purple
        default:
            return lv_color_make(40, 40, 40);   // dark grey for unconfigured
    }
}

// ── Table draw event — colour rows by pin mode ────────────────────────────
//
// LVGL 8 table: draw event fires per cell.
// dsc->id = row * TABLE_COLS + col  (linear cell index)
// Row 0 is the header row — leave uncoloured.

static void table_draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS) return;

    uint32_t row = dsc->id / TABLE_COLS;
    if (row == 0) {
        // Header row — dark background so white text stays readable
        dsc->rect_dsc->bg_color = lv_color_make(50, 50, 70);
        dsc->rect_dsc->bg_opa   = LV_OPA_COVER;
        return;
    }

    int pin_idx = (int)row - 1;
    if (pin_idx < 0 || pin_idx >= s_snap_count) return;

    // rect_dsc is the lv_draw_rect_dsc_t* set by lv_table.c before firing this event
    dsc->rect_dsc->bg_color = row_bg_color(&s_snap[pin_idx]);
    dsc->rect_dsc->bg_opa   = LV_OPA_COVER;
}

// ── Display timer — runs every 500 ms from the LVGL task ─────────────────

static void ui_update_timer_cb(lv_timer_t *t)
{
    (void)t;

    // Poll fresh digital levels (INPUT/OUTPUT register reads only — no ADC).
    // ADC rows show last-known value from the most recent read_pins tool call.
    gpio_state_poll_digital(s_snap, SAFE_DIGITAL_COUNT, &s_snap_count);

    for (int i = 0; i < s_snap_count; i++) {
        const pin_state_t *s = &s_snap[i];

        // Col 0: GPIO number
        char gpio_str[8];
        snprintf(gpio_str, sizeof(gpio_str), "%d", s->gpio);
        lv_table_set_cell_value(s_table, (uint16_t)(i + 1), 0, gpio_str);

        // Col 1: Mode
        lv_table_set_cell_value(s_table, (uint16_t)(i + 1), 1, pin_mode_str(s->mode));

        // Col 2: Value — format depends on mode
        char val_str[16];
        if (s->mode == PIN_MODE_UNCONFIGURED || s->value < 0) {
            snprintf(val_str, sizeof(val_str), "--");
        } else if (s->mode == PIN_MODE_ADC) {
            snprintf(val_str, sizeof(val_str), "%dmV", s->value);
        } else if (s->mode == PIN_MODE_PWM) {
            snprintf(val_str, sizeof(val_str), "%d%%", s->value);
        } else {
            snprintf(val_str, sizeof(val_str), "%s", s->value ? "HIGH" : "LOW");
        }
        lv_table_set_cell_value(s_table, (uint16_t)(i + 1), 2, val_str);
    }

    // Force a redraw so colour changes from draw event are applied
    lv_obj_invalidate(s_table);
}

// ── Public API ────────────────────────────────────────────────────────────

void ui_display_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);

    // Title label
    s_lbl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_title, "MCP GPIO");
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 6);

    // IP / status label
    s_lbl_ip = lv_label_create(scr);
    lv_label_set_text(s_lbl_ip, "---");
    lv_obj_set_style_text_color(s_lbl_ip, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_align(s_lbl_ip, LV_ALIGN_TOP_MID, 0, 28);

    // Pin state table
    s_table = lv_table_create(scr);
    lv_obj_set_width(s_table, 172);
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(s_table, lv_color_black(), LV_PART_MAIN);

    // Column widths
    lv_table_set_col_width(s_table, 0, COL_GPIO_W);
    lv_table_set_col_width(s_table, 1, COL_MODE_W);
    lv_table_set_col_width(s_table, 2, COL_VAL_W);

    // Total rows: 1 header + SAFE_DIGITAL_COUNT data rows
    lv_table_set_row_cnt(s_table, (uint16_t)(SAFE_DIGITAL_COUNT + 1));
    lv_table_set_col_cnt(s_table, TABLE_COLS);

    // Header row
    lv_table_set_cell_value(s_table, 0, 0, "GPIO");
    lv_table_set_cell_value(s_table, 0, 1, "Mode");
    lv_table_set_cell_value(s_table, 0, 2, "Value");

    // Data rows — populate with initial "unconfigured" state
    for (int i = 0; i < SAFE_DIGITAL_COUNT; i++) {
        char gpio_str[8];
        snprintf(gpio_str, sizeof(gpio_str), "%d", SAFE_DIGITAL_PINS[i].gpio);
        lv_table_set_cell_value(s_table, (uint16_t)(i + 1), 0, gpio_str);
        lv_table_set_cell_value(s_table, (uint16_t)(i + 1), 1, "---");
        lv_table_set_cell_value(s_table, (uint16_t)(i + 1), 2, "--");
    }

    // White text on all cells — needed because row backgrounds are dark colours
    lv_obj_set_style_text_color(s_table, lv_color_white(), LV_PART_ITEMS);

    // Reduce cell padding so all 9 rows fit on screen.
    // Default: pad_top/bottom = 8px → 30px per row → 9 rows = 270px (overflows 268px).
    // With 3px: 14 + 3 + 3 = 20px per row → 9 rows = 180px → fits with room to spare.
    lv_obj_set_style_pad_top(s_table,    3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_table,   4, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_table,  4, LV_PART_ITEMS);

    // Fix height to remaining screen space so nothing overflows
    lv_obj_set_height(s_table, 320 - 52);

    // Disable scrolling — all rows fit on screen
    lv_obj_clear_flag(s_table, LV_OBJ_FLAG_SCROLLABLE);

    // Row colour draw callback
    lv_obj_add_event_cb(s_table, table_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    // Seed snapshot so draw callback has valid data before first timer tick
    gpio_state_snapshot(s_snap, SAFE_DIGITAL_COUNT, &s_snap_count);

    // Create the 500 ms refresh timer HERE — ui_display_init() is called before
    // the LVGL task starts, so lv_timer_create() is safe. Creating it later from
    // app_main (while the LVGL task is running) risks corrupting the timer list.
    lv_timer_create(ui_update_timer_cb, 500, NULL);

    ESP_LOGI(TAG, "Dashboard initialised (%d pins)", SAFE_DIGITAL_COUNT);
}

void ui_display_show_connecting(void)
{
    // Called before LVGL task starts — safe to call directly from app_main.
    lv_label_set_text(s_lbl_ip, "Connecting...");
}

void ui_display_show_ready(const char *ip)
{
    // Called from app_main after Wi-Fi connects. LVGL task is already running,
    // so only update the label text — no timer creation or layout ops here.
    lv_label_set_text(s_lbl_ip, ip);
    ESP_LOGI(TAG, "Ready, IP=%s", ip);
}
