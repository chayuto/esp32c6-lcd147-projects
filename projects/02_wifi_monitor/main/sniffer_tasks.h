#pragma once

#include <stdint.h>
#include <stdbool.h>

// Start consumer, stats, and reporter tasks.
// enable_auto_hop: true = start channel-hop task; false = manual control via button.
void sniffer_tasks_start(bool enable_auto_hop);

// Change active channel and reset all per-channel state.
void sniffer_tasks_set_channel(uint8_t channel);
