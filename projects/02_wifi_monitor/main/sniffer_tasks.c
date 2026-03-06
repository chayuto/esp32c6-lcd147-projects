#include "sniffer_tasks.h"
#include "app_state.h"
#include "wifi_sniffer.h"
#include "airtime.h"
#include "channel_util.h"
#include "rf_metrics.h"
#include "device_counter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include <limits.h>

static const char *TAG = "tasks";

// --- Static task storage ---
#define CONSUMER_STACK_WORDS  (8192  / sizeof(StackType_t))
#define STATS_STACK_WORDS     (4096  / sizeof(StackType_t))
#define REPORTER_STACK_WORDS  (2048  / sizeof(StackType_t))
#define CHANHOP_STACK_WORDS   (1024  / sizeof(StackType_t))

static StackType_t  s_consumer_stack[CONSUMER_STACK_WORDS];
static StaticTask_t s_consumer_tcb;

static StackType_t  s_stats_stack[STATS_STACK_WORDS];
static StaticTask_t s_stats_tcb;

static StackType_t  s_reporter_stack[REPORTER_STACK_WORDS];
static StaticTask_t s_reporter_tcb;

static StackType_t  s_chanhop_stack[CHANHOP_STACK_WORDS];
static StaticTask_t s_chanhop_tcb;

static TaskHandle_t s_stats_handle    = NULL;
static TaskHandle_t s_chanhop_handle  = NULL;

#define NOTIFY_BATCH  50

// Channel rotation for auto-hop
static const uint8_t k_channels[] = {1, 6, 11};
static uint8_t s_ch_idx = 1; // start on channel 6

// --- consumer_task ---
static void consumer_task(void *arg)
{
    esp_task_wdt_add(NULL);

    QueueHandle_t q = wifi_sniffer_get_queue();
    PacketMeta m;
    uint32_t batch = 0;

    for (;;) {
        BaseType_t got = xQueueReceive(q, &m, pdMS_TO_TICKS(100));
        esp_task_wdt_reset();

        if (got == pdTRUE) {
            int64_t now_us = m.timestamp_us;

            double airtime = estimate_airtime_us(&m);
            cu_add(now_us, airtime);

            rf_metrics_update(&m, now_us, cu_get_pct());

            if (m.is_probe_req) {
                device_counter_add(&m, now_us);
            }
        }

        // Notify stats_task every NOTIFY_BATCH packets (or on timeout)
        if (++batch >= NOTIFY_BATCH) {
            xTaskNotify(s_stats_handle, batch, eSetValueWithOverwrite);
            batch = 0;
        }
    }
}

// --- stats_task ---
static void stats_task(void *arg)
{
    uint32_t notif_val;

    for (;;) {
        xTaskNotifyWait(0, ULONG_MAX, &notif_val, portMAX_DELAY);

        double cu                = cu_get_pct();
        uint32_t pkt_per_sec     = cu_get_pkt_per_sec();
        rf_metrics_snapshot_t rf = rf_metrics_get_snapshot();
        uint32_t dev_count       = device_counter_get_count();

        uint32_t rx, dropped;
        wifi_sniffer_get_stats(&rx, &dropped);
        uint8_t drop_pct = (rx + dropped > 0)
                           ? (uint8_t)((dropped * 100) / (rx + dropped))
                           : 0;

        if (xSemaphoreTake(g_metrics_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_metrics.cu_pct          = cu;
            g_metrics.rf_score        = rf.rf_score;
            g_metrics.avg_snr_db      = rf.avg_snr_db;
            g_metrics.avg_noise_floor = rf.avg_noise_floor;
            g_metrics.device_count    = dev_count;
            g_metrics.pkt_per_sec     = pkt_per_sec;
            g_metrics.drop_pct        = drop_pct;
            g_metrics.bssid_count     = rf.bssid_count;
            xSemaphoreGive(g_metrics_mutex);
        }
    }
}

// --- reporter_task ---
static void reporter_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (xSemaphoreTake(g_metrics_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            analyzer_metrics_t s = g_metrics;
            xSemaphoreGive(g_metrics_mutex);
            ESP_LOGI(TAG, "CH%d | CU=%.1f%% RF=%.0f SNR=%.1fdB NF=%ddBm "
                          "DEV=%lu PKT/S=%lu DROP=%d%%",
                     s.active_channel, s.cu_pct, s.rf_score,
                     s.avg_snr_db, (int)s.avg_noise_floor,
                     (unsigned long)s.device_count,
                     (unsigned long)s.pkt_per_sec,
                     s.drop_pct);
        }
    }
}

// --- channel_hop_task ---
static void channel_hop_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SNIFFER_CHANNEL_DWELL_MS));
        s_ch_idx = (s_ch_idx + 1) % 3;
        sniffer_tasks_set_channel(k_channels[s_ch_idx]);
    }
}

// --- Public API ---
void sniffer_tasks_set_channel(uint8_t channel)
{
    wifi_sniffer_set_channel(channel);
    cu_reset();
    rf_metrics_reset();
    device_counter_reset();

    if (xSemaphoreTake(g_metrics_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_metrics.active_channel = channel;
        g_metrics.cu_pct         = 0.0;
        g_metrics.rf_score       = 100.0;
        g_metrics.device_count   = 0;
        g_metrics.pkt_per_sec    = 0;
        xSemaphoreGive(g_metrics_mutex);
    }
    ESP_LOGI(TAG, "Channel -> %d", channel);
}

void sniffer_tasks_start(bool enable_auto_hop)
{
    device_counter_init();

    // consumer — priority 10
    xTaskCreateStatic(consumer_task, "consumer",
                      CONSUMER_STACK_WORDS, NULL, 10,
                      s_consumer_stack, &s_consumer_tcb);

    // stats — priority 5
    s_stats_handle = xTaskCreateStatic(stats_task, "stats",
                      STATS_STACK_WORDS, NULL, 5,
                      s_stats_stack, &s_stats_tcb);

    // reporter — priority 3
    xTaskCreateStatic(reporter_task, "reporter",
                      REPORTER_STACK_WORDS, NULL, 3,
                      s_reporter_stack, &s_reporter_tcb);

    if (enable_auto_hop) {
        s_chanhop_handle = xTaskCreateStatic(channel_hop_task, "chanhop",
                              CHANHOP_STACK_WORDS, NULL, 2,
                              s_chanhop_stack, &s_chanhop_tcb);
    }

    ESP_LOGI(TAG, "Sniffer tasks started (auto_hop=%d)", enable_auto_hop);
}
