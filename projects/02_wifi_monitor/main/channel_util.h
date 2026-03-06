#pragma once

#include <stdint.h>

// Add a packet's air time to the 1-second rolling window.
// Returns current channel utilization percentage (0.0–100.0).
double cu_add(int64_t now_us, double airtime_us);

// Query current CU% without adding a sample.
double cu_get_pct(void);

// Packets received within the last 1-second window.
uint32_t cu_get_pkt_per_sec(void);

// Reset all state (call on channel change).
void cu_reset(void);
