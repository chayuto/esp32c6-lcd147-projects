#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Dwell time per channel in auto-hop mode
#define SNIFFER_CHANNEL_DWELL_MS  330
// Queue depth — 256 items × 48 bytes = 12 KB static
#define SNIFFER_QUEUE_DEPTH       256

// One captured packet, extracted in the promiscuous callback.
// Exactly 48 bytes — verified by _Static_assert in wifi_sniffer.c
typedef struct __attribute__((packed)) {
    int64_t  timestamp_us;    // esp_timer_get_time()
    int8_t   rssi;            // signed dBm
    int8_t   noise_floor;     // signed dBm
    uint8_t  bb_format;       // wifi_rx_bb_format_t cast to uint8_t
    uint8_t  frame_type;      // (f[0] >> 2) & 0x03
    uint8_t  frame_subtype;   // (f[0] >> 4) & 0x0F
    uint8_t  channel;         // rx_ctrl.channel
    uint8_t  retry;           // (f[1] >> 3) & 0x01
    uint8_t  is_probe_req;    // 1 if probe request with valid ie_hash
    uint16_t pkt_length;      // rx_ctrl.sig_len (MPDU including FCS)
    uint16_t seq_ctrl;        // raw sequence control field
    uint8_t  addr_src[6];     // transmitter MAC (Address 2)
    uint8_t  addr_bssid[6];   // BSSID (Address 3)
    uint64_t ie_hash;         // FNV-1a 64-bit of IEs (valid if is_probe_req)
    uint32_t he_siga1;        // raw HE-SIG-A1 (or HT-SIG for HT frames)
    uint16_t he_siga2;        // raw HE-SIG-A2
    uint8_t  _pad[2];         // alignment padding
} PacketMeta;

// Initialise Wi-Fi stack and start promiscuous mode on the given channel.
esp_err_t wifi_sniffer_init(uint8_t initial_channel);

// Change the active scan channel.
void wifi_sniffer_set_channel(uint8_t channel);

// Returns the queue handle for consumer_task.
QueueHandle_t wifi_sniffer_get_queue(void);

// Fill cumulative packet counts since init.
void wifi_sniffer_get_stats(uint32_t *received, uint32_t *dropped);
