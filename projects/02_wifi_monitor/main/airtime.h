#pragma once

#include "wifi_sniffer.h"

// Estimate air time in microseconds for a single captured packet.
// Uses bb_format to dispatch to the correct PHY formula.
double estimate_airtime_us(const PacketMeta *m);
