#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APP_STATE_BOOT = 0,
    APP_STATE_SCANNING,
    APP_STATE_ERROR,
} analyzer_state_t;

typedef struct {
    double   cu_pct;           // Channel utilization 0.0–100.0%
    double   rf_score;         // RF quality index 0.0–100.0
    double   avg_snr_db;       // Exponential moving average SNR in dB
    int8_t   avg_noise_floor;  // dBm, EMA
    uint32_t device_count;     // Estimated unique devices
    uint32_t pkt_per_sec;      // Packets received per second
    uint8_t  drop_pct;         // Queue drop rate 0–100%
    uint8_t  active_channel;   // Currently scanned channel: 1, 6, or 11
    uint32_t bssid_count;      // Unique BSSIDs seen in last 60s
} analyzer_metrics_t;

// Globals defined in main.c
extern volatile analyzer_state_t g_app_state;
extern analyzer_metrics_t        g_metrics;
extern SemaphoreHandle_t         g_metrics_mutex;

static inline analyzer_state_t app_state_get(void) { return g_app_state; }
static inline void app_state_set(analyzer_state_t s) { g_app_state = s; }
