# ESP32-C6 LCD Display UI Reference

Reference for LVGL 8.3.11 UI work on the Waveshare ESP32-C6-LCD-1.47.
Invoke when designing, debugging, or modifying any `ui_display.c` screen.

---

## Screen Geometry

```
Width  : 172 px  (narrow — fits about 18 chars at font_montserrat_14)
Height : 320 px  (tall portrait — useful for lists/tables)
Origin : top-left (0,0)
Colour : 16-bit RGB565 via ST7789 SPI
```

### Usable layout budget (typical header + table pattern)

| Zone         | Y start | Height  | Notes                        |
|---|---|---|---|
| Title label  | 6       | ~20 px  | font_montserrat_14 bold      |
| Status label | 28      | ~20 px  | font_montserrat_14 normal    |
| Table / body | 52      | 268 px  | 320 - 52 = 268 px remaining  |

---

## Font Sizes Available (LVGL 8 built-ins)

Only fonts enabled in `lv_conf.h` are available. Confirmed enabled in this repo:

| Symbol                    | Actual height | Best for                  |
|---|---|---|
| `&lv_font_montserrat_10`  | 10 px         | tiny labels, units        |
| `&lv_font_montserrat_14`  | 14 px         | **default** — table cells |
| `&lv_font_montserrat_18`  | 18 px         | section headers           |
| `&lv_font_montserrat_24`  | 24 px         | large values / clocks     |

Set font: `lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN)`

---

## lv_table Row Height Calculation

```
row_height = font_height + pad_top + pad_bottom
           = 14          + 8       + 8           = 30 px  (LVGL default)
           = 14          + 3       + 3           = 20 px  (compact — use this)
```

**For 9 rows (1 header + 8 data) in 268 px:**
- Default 30 px/row → 270 px → OVERFLOWS by 2 px (rows clip off screen)
- Compact 20 px/row → 180 px → fits with 88 px spare

Always set compact padding when using 8+ data rows:
```c
lv_obj_set_style_pad_top(table,    3, LV_PART_ITEMS);
lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
lv_obj_set_style_pad_left(table,   4, LV_PART_ITEMS);
lv_obj_set_style_pad_right(table,  4, LV_PART_ITEMS);
lv_obj_set_height(table, 268);   // clamp to available space
```

---

## Column Width Budget (172 px total)

No built-in horizontal scroll — columns must sum to ≤ 172 px.
LVGL table adds 1 px border between columns; account for it.

| Layout               | Col widths          | Sum  |
|---|---|---|
| GPIO + Mode + Value  | 38 + 88 + 46 = 172  | 172  |
| GPIO + Mode + Value  | 32 + 96 + 44 = 172  | 172  |
| Key + Value (2-col)  | 80 + 92 = 172       | 172  |

Characters that fit per column at font_montserrat_14 (~8 px/char):
- 38 px → ~4 chars ("GPIO", " 23")
- 88 px → ~10 chars ("OUTPUT", "INPUT", "ADC")
- 46 px → ~5 chars ("HIGH", "1234mV")

---

## Color Rules

**Background is always black** (`lv_color_black()`).
All text must be explicitly set to white or light colours — LVGL default is dark.

```c
// Screen background
lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);

// Title / labels — always explicit white
lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);

// Table cells — must set LV_PART_ITEMS, not LV_PART_MAIN
lv_obj_set_style_text_color(table, lv_color_white(), LV_PART_ITEMS);
```

**Table header row**: LVGL default background is light. Override in draw callback:
```c
if (row == 0) {
    dsc->rect_dsc->bg_color = lv_color_make(50, 50, 70);  // dark blue-grey
    dsc->rect_dsc->bg_opa   = LV_OPA_COVER;
    return;
}
```

**Muted status colours** (readable on bright LCD in daylight):

| State         | lv_color_make(r, g, b) | Appearance     |
|---|---|---|
| OUTPUT HIGH   | (0, 130, 0)            | dark green     |
| OUTPUT LOW    | (150, 0, 0)            | dark red       |
| INPUT         | (160, 100, 0)          | amber          |
| ADC           | (0, 80, 170)           | blue           |
| PWM           | (100, 0, 160)          | purple         |
| UNCONFIGURED  | (40, 40, 40)           | dark grey      |
| Header row    | (50, 50, 70)           | dark blue-grey |

**Use `lv_color_make(r,g,b)` — NOT `LV_COLOR_MAKE(R,G,B)`** (uppercase macro is a
compound literal — causes "expected expression" compile error in function call arguments).

---

## WS2812 RGB LED (GPIO 8)

**R and G channels are physically swapped on this board.**
Always swap r/g in the `set_pixel` wrapper:
```c
static void set_pixel(uint8_t r, uint8_t g, uint8_t b) {
    led_strip_set_pixel(strip, 0, g, r, b);   // g and r swapped
    led_strip_refresh(strip);
}
```

---

## LVGL Thread Safety Rules (Single-Core ESP32-C6)

| Where called from   | Safe operations                          | Unsafe                     |
|---|---|---|
| Before LVGL task    | Everything — lv_timer_create, layout ops | —                          |
| Inside lv_timer cb  | Everything                               | —                          |
| app_main (post-task)| lv_label_set_text (fast, no layout)      | lv_timer_create, lv_obj_align |
| FreeRTOS tasks      | NOTHING directly — use shared state + timer polling | All LVGL APIs |

**Golden rule**: Create timers and build layout in `ui_display_init()` (called before
`xTaskCreate(lvgl_task)`). Never call `lv_timer_create` from app_main after the
LVGL task has started — it can corrupt the timer linked list on a tick boundary.

---

## LVGL Task Setup (standard pattern)

```c
static void lvgl_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
// Priority 1, stack 16384 bytes
xTaskCreate(lvgl_task, "lvgl", 16384, NULL, 1, NULL);
```

`lv_tick_inc()` is called by the lcd_driver component via esp_timer — do not call it manually.

---

## lv_table Draw Event (row colouring)

```c
static void table_draw_event_cb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS) return;

    uint32_t row = dsc->id / TABLE_COLS;   // id = row * col_cnt + col
    // row == 0 is the header row

    dsc->rect_dsc->bg_color = ...;   // set background per row
    dsc->rect_dsc->bg_opa   = LV_OPA_COVER;
    // dsc->label_dsc->color for per-cell text override (rarely needed)
}
lv_obj_add_event_cb(table, cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
```

`dsc->id` confirmed: `row * col_cnt + col` (verified from lv_table.c line 813).
`dsc->rect_dsc` confirmed: set from `lv_table.c:719` before event fires.

---

## Common Pitfalls

1. **Table clips at bottom** — rows overflow available height. Fix: reduce pad_top/bottom + set explicit height.
2. **Text invisible on dark background** — forgot `lv_obj_set_style_text_color(..., LV_PART_ITEMS)`.
3. **Header row text invisible** — LVGL default header bg is light; override in draw callback.
4. **Timer never fires** — called `lv_timer_create` from app_main after LVGL task started. Fix: call it in `ui_display_init()`.
5. **LED wrong colour** — R/G physically swapped on this board; swap in `set_pixel()`.
6. **Blank screen after LVGL mutation from FreeRTOS task** — use shared state + timer polling pattern.
7. **`LV_COLOR_MAKE` compile error** — use `lv_color_make()` lowercase in expressions.
