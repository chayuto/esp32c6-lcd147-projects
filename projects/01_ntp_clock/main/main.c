#include "app_state.h"
#include "wifi_connect.h"
#include "ntp_sync.h"
#include "theme.h"
#include "led_ctrl.h"
#include "button.h"
#include "boot_screen.h"
#include "clock_face.h"

#include "ST7789.h"
#include "LVGL_Driver.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"

#define TAG "main"

// --- Shared state ---
volatile app_state_t g_app_state = APP_STATE_BOOT;
SemaphoreHandle_t    g_state_mutex;

static QueueHandle_t s_btn_queue;
static bool          s_led_enabled = true;

// Shared detail string for boot screen — written by network_task, read by LVGL timer
static char s_boot_detail[32] = {0};

// --- WiFi + NTP task ---
static void network_task(void *arg)
{
    app_state_set(APP_STATE_WIFI_CONNECTING);

    esp_err_t ret = wifi_connect(
        CONFIG_WIFI_SSID,
        CONFIG_WIFI_PASSWORD,
        CONFIG_WIFI_CONNECT_TIMEOUT_MS
    );

    if (ret != ESP_OK) {
        app_state_set(APP_STATE_WIFI_ERROR);
        // Retry after 10s
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    ESP_LOGI(TAG, "WiFi connected: %s", ip);
    snprintf(s_boot_detail, sizeof(s_boot_detail), "%s", ip);
    app_state_set(APP_STATE_NTP_SYNCING);

    led_ctrl_ntp_flash();

    ret = ntp_sync_start(CONFIG_NTP_SERVER, CONFIG_TIMEZONE_POSIX, 10000);
    if (ret != ESP_OK) {
        app_state_set(APP_STATE_NTP_ERROR);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI(TAG, "NTP done — setting CLOCK_RUNNING");
    app_state_set(APP_STATE_CLOCK_RUNNING);
    ESP_LOGI(TAG, "network_task done, deleting self");
    vTaskDelete(NULL);
}

// --- LVGL + clock update timer ---
static void clock_tick_cb(lv_timer_t *timer)
{
    static app_state_t last_state = APP_STATE_BOOT;
    app_state_t state = app_state_get();

    // Update boot screen on state transitions (safe — inside LVGL timer)
    if (state != last_state) {
        ESP_LOGI(TAG, "State: %d -> %d", last_state, state);
        if (state == APP_STATE_CLOCK_RUNNING) {
            // Init clock face FIRST (loads new screen), then destroy boot screen.
            // Never delete the active screen before a new one is loaded.
            ESP_LOGI(TAG, "Transition: boot -> clock");
            clock_face_init();
            ESP_LOGI(TAG, "clock_face_init done");
            boot_screen_destroy();
            ESP_LOGI(TAG, "boot_screen_destroy done");
            clock_face_set_wifi_status(true);
            clock_face_set_ntp_status(true);
            led_ctrl_set_theme();
            ESP_LOGI(TAG, "Clock face active");
        } else if (state == APP_STATE_NTP_SYNCING) {
            boot_screen_update(state, s_boot_detail);
        } else {
            boot_screen_update(state, NULL);
        }
    }
    last_state = state;

    // Handle button events
    btn_event_t btn;
    if (xQueueReceive(s_btn_queue, &btn, 0) == pdTRUE) {
        if (btn == BTN_SHORT_PRESS && state == APP_STATE_CLOCK_RUNNING) {
            int next = (theme_current_index() + 1) % theme_count;
            theme_apply(next);
            clock_face_apply_theme();
            ESP_LOGI(TAG, "Theme: %s", theme_current()->name);
        } else if (btn == BTN_LONG_PRESS) {
            s_led_enabled = !s_led_enabled;
            led_ctrl_set_enabled(s_led_enabled);
        }
    }

    // Update clock
    if (state == APP_STATE_CLOCK_RUNNING) {
        struct tm timeinfo;
        ntp_get_local_time(&timeinfo);
        ESP_LOGD(TAG, "tick %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        clock_face_update(&timeinfo);
    }
}

// --- LVGL task ---
static void lvgl_task(void *arg)
{
    // 1s clock update timer
    lv_timer_create(clock_tick_cb, 1000, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    g_state_mutex = xSemaphoreCreateMutex();

    // Hardware init
    LCD_Init();
    BK_Light(80);
    LVGL_Init();
    led_ctrl_init();

    // Boot screen
    boot_screen_init();
    led_ctrl_boot_breathing();

    // Button
    s_btn_queue = button_create_queue();
    button_init(s_btn_queue);

    // Network task
    xTaskCreate(network_task, "net_task", 8192, NULL, 2, NULL);

    // LVGL task (runs lv_timer_handler + clock updates)
    xTaskCreate(lvgl_task, "lvgl_task", 16384, NULL, 1, NULL);
}
