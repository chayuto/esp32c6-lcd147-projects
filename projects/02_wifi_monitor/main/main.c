#include "app_state.h"
#include "wifi_sniffer.h"
#include "sniffer_tasks.h"
#include "ui_display.h"
#include "led_ctrl.h"
#include "button.h"

#include "ST7789.h"
#include "LVGL_Driver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "main";

// Globals declared extern in app_state.h
volatile analyzer_state_t g_app_state  = APP_STATE_BOOT;
analyzer_metrics_t        g_metrics    = {0};
SemaphoreHandle_t         g_metrics_mutex;

static QueueHandle_t s_btn_queue;
static bool          s_led_enabled = true;

// Channel cycle: 1 → 6 → 11 → 1
static const uint8_t k_channels[] = {1, 6, 11};
static int           s_ch_idx     = 1; // start on channel 6

// Boot screen objects (destroyed after first metrics snapshot)
static lv_obj_t *s_boot_scr = NULL;

static void boot_screen_show(void)
{
    s_boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_boot_scr, lv_color_make(0x0D, 0x11, 0x17), 0);
    lv_obj_set_style_bg_opa(s_boot_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_boot_scr, 0, 0);

    lv_obj_t *spinner = lv_spinner_create(s_boot_scr, 1000, 270);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spinner, lv_color_make(0x00, 0xB4, 0xD8), LV_PART_INDICATOR);

    lv_obj_t *lbl = lv_label_create(s_boot_scr);
    lv_label_set_text(lbl, "Scanning...");
    lv_obj_set_style_text_color(lbl, lv_color_make(0xAD, 0xB5, 0xBD), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 50);

    lv_scr_load(s_boot_scr);
}

// LVGL timer — runs every 2000 ms in the LVGL task context
static void ui_tick_cb(lv_timer_t *timer)
{
    static bool metrics_active = false;

    // Transition boot → metrics screen once we have real data
    if (!metrics_active && g_app_state == APP_STATE_SCANNING) {
        bool has_data = false;
        if (xSemaphoreTake(g_metrics_mutex, 0) == pdTRUE) {
            has_data = (g_metrics.pkt_per_sec > 0 || g_metrics.cu_pct > 0.0);
            xSemaphoreGive(g_metrics_mutex);
        }
        if (has_data) {
            ui_display_init();          // load metrics screen
            if (s_boot_scr) {
                lv_obj_del(s_boot_scr); // destroy boot screen after new screen loads
                s_boot_scr = NULL;
            }
            metrics_active = true;
            ESP_LOGI(TAG, "Metrics screen active");
        }
    }

    if (!metrics_active) return;

    // Button events
    btn_event_t btn;
    if (xQueueReceive(s_btn_queue, &btn, 0) == pdTRUE) {
        if (btn == BTN_SHORT_PRESS) {
            s_ch_idx = (s_ch_idx + 1) % 3;
            uint8_t ch = k_channels[s_ch_idx];
            sniffer_tasks_set_channel(ch);
            ESP_LOGI(TAG, "Channel -> %d", ch);
        } else if (btn == BTN_LONG_PRESS) {
            s_led_enabled = !s_led_enabled;
            led_ctrl_set_enabled(s_led_enabled);
        }
    }

    // Update display + LED (non-blocking mutex attempt)
    if (xSemaphoreTake(g_metrics_mutex, 0) == pdTRUE) {
        analyzer_metrics_t snap = g_metrics;
        xSemaphoreGive(g_metrics_mutex);
        ui_display_update(&snap);
        if (s_led_enabled) led_ctrl_set_cu(snap.cu_pct);
    }
}

// LVGL task — calls lv_timer_handler every 10 ms
static void lvgl_task(void *arg)
{
    lv_timer_create(ui_tick_cb, 2000, NULL);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

void app_main(void)
{
    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Shared mutex
    g_metrics_mutex = xSemaphoreCreateMutex();

    // 3. Hardware
    LCD_Init();
    BK_Light(80);
    LVGL_Init();
    led_ctrl_init();

    // 4. Boot screen (LVGL objects — after LVGL_Init)
    boot_screen_show();
    led_ctrl_boot_breathing();

    // 5. Button
    s_btn_queue = button_create_queue();
    button_init(s_btn_queue);

    // 6. Wi-Fi sniffer — start on channel 6
    ESP_ERROR_CHECK(wifi_sniffer_init(k_channels[s_ch_idx]));

    // 7. Set initial channel in shared metrics
    if (xSemaphoreTake(g_metrics_mutex, portMAX_DELAY) == pdTRUE) {
        g_metrics.active_channel = k_channels[s_ch_idx];
        xSemaphoreGive(g_metrics_mutex);
    }

    // 8. Pipeline tasks
    sniffer_tasks_start(false); // manual channel control via button
    app_state_set(APP_STATE_SCANNING);

    // 9. LVGL task (owns boot screen render + all UI mutations)
    xTaskCreate(lvgl_task, "lvgl_task", 16384, NULL, 1, NULL);

    ESP_LOGI(TAG, "wifi_monitor started on CH%d", k_channels[s_ch_idx]);
}
