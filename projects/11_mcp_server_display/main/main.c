#include "drawing_engine.h"
#include "ui_display.h"
#include "mcp_server.h"

#include "ST7789.h"
#include "LVGL_Driver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "main";

// LVGL task — drives lv_timer_handler every 10 ms.
// ui_render_timer_cb is registered at 50 ms to drain the draw queue.
static void lvgl_task(void *arg)
{
    lv_timer_create(ui_render_timer_cb, 50, NULL);
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

    // 3. Drawing pipeline (static queue + canvas + mutex)
    drawing_engine_init();
    ui_display_init();

    // 4. Queue "Connecting..." screen — rendered once lvgl_task starts
    ui_display_show_connecting();

    // 5. Start LVGL task — display animates while app_main blocks on Wi-Fi
    xTaskCreate(lvgl_task, "lvgl_task", 16384, NULL, 1, NULL);

    // 6. Wi-Fi STA — blocks up to 30 s; modem sleep disabled on connect
    char ip[16] = {0};
    if (wifi_init_sta(ip, sizeof(ip)) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed — halting");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 7. Update display with IP + ready message
    ui_display_show_ip(ip);

    // 8. mDNS + HTTP server + cached tools/list JSON
    ESP_ERROR_CHECK(mcp_server_start());

    ESP_LOGI(TAG, "MCP server ready at http://%s/mcp  (esp32-canvas.local)", ip);
}
