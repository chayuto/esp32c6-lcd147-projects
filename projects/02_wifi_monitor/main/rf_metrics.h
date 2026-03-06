#pragma once

#include "wifi_sniffer.h"
#include <stdint.h>

typedef struct {
    double   avg_snr_db;
    int8_t   avg_noise_floor;
    double   retry_pct;
    uint32_t bssid_count;
    double   rf_score;
} rf_metrics_snapshot_t;

// Process one packet. Call from consumer_task only.
void rf_metrics_update(const PacketMeta *m, int64_t now_us, double cu_pct);

// Get a snapshot of current metrics.
rf_metrics_snapshot_t rf_metrics_get_snapshot(void);

// Reset all state (call on channel change).
void rf_metrics_reset(void);
