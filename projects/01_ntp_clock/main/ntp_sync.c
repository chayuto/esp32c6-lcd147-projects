#include "ntp_sync.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "esp_log.h"

#define TAG "ntp"

static bool s_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "Time synced");
}

esp_err_t ntp_sync_start(const char *server, const char *timezone, uint32_t timeout_ms)
{
    setenv("TZ", timezone, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_setservername(1, "time.cloudflare.com");
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_set_sync_interval(3600000); // resync every hour
    esp_sntp_init();

    ESP_LOGI(TAG, "Waiting for NTP sync (server: %s, tz: %s)...", server, timezone);

    uint32_t elapsed = 0;
    while (!s_synced && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }

    if (!s_synced) {
        ESP_LOGW(TAG, "NTP sync timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool ntp_is_synced(void) { return s_synced; }

void ntp_get_local_time(struct tm *out)
{
    time_t now;
    time(&now);
    localtime_r(&now, out);
}
