#pragma once

#include "wifi_sniffer.h"
#include <stdint.h>

// Initialise device counter and generate first salt.
void device_counter_init(void);

// Process one probe request. Only effective when m->is_probe_req == 1.
// Call from consumer_task only.
void device_counter_add(const PacketMeta *m, int64_t now_us);

// Return estimated unique device count in current hour window.
uint32_t device_counter_get_count(void);

// Reset observations (call on channel change).
void device_counter_reset(void);
