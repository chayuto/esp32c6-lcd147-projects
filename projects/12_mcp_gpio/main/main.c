#include "gpio_state.h"
#include "mcp_server.h"
#include "ui_display.h"
#include "led_status.h"

#include "ST7789.h"
#include "LVGL_Driver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "main";

// LVGL task — drives lv_timer_handler every 10 ms.
// The ui_update_timer_cb (registered at 500 ms in ui_display_show_ready)
// runs inside this task, which is why all LVGL mutations are safe here.
static void lvgl_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

void app_main(void)
{
    // 1. NVS — required by Wi-Fi driver
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 2. Display hardware
    LCD_Init();
    BK_Light(80);
    LVGL_Init();

    // 3. GPIO state machine — initialises pin table and ADC oneshot unit
    ESP_ERROR_CHECK(gpio_state_init());

    // 4. RGB LED status indicator (GPIO 8, WS2812)
    led_status_init();
    led_status_connecting();

    // 5. Build LVGL widgets (table, labels) — must happen before LVGL task
    ui_display_init();
    ui_display_show_connecting();

    // 6. Start LVGL task at low priority — display animates while app_main
    //    blocks below on Wi-Fi association (up to 30 s)
    xTaskCreate(lvgl_task, "lvgl", 16384, NULL, 1, NULL);

    // 7. Wi-Fi + MCP server
    char ip[24] = {0};
    if (wifi_init_sta(ip, sizeof(ip)) != ESP_OK) {
        // On failure show red LED + keep "Connecting..." on display
        ESP_LOGE(TAG, "Wi-Fi failed — check sdkconfig.defaults credentials");
        led_status_error();
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(mcp_server_start());

    // 8. Update display and LED — server is live
    led_status_ready();
    ui_display_show_ready(ip);

    ESP_LOGI(TAG, "MCP GPIO server ready at http://%s/mcp", ip);
    // app_main can now return — all work is driven by the HTTP server task
    // and the LVGL task created above.
}
