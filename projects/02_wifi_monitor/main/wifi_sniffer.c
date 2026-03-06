#include "wifi_sniffer.h"

#include "esp_wifi.h"
#include "esp_wifi_he_types.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdint.h>

static const char *TAG = "sniffer";

_Static_assert(sizeof(PacketMeta) == 48, "PacketMeta must be exactly 48 bytes");

// Static queue — no heap allocation
static StaticQueue_t s_queue_struct;
static uint8_t       s_queue_buf[SNIFFER_QUEUE_DEPTH * sizeof(PacketMeta)];
static QueueHandle_t s_queue;

// Cumulative counters
static volatile uint32_t s_rx_count   = 0;
static volatile uint32_t s_drop_count = 0;

// FNV-1a 64-bit hash — fast non-cryptographic hash suitable for the callback context
static uint64_t fnv1a_64(const uint8_t *data, int len)
{
    uint64_t hash = 14695981039346656037ULL;
    for (int i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Hash the Information Elements from a probe request.
// Skips SSID (tag 0) to avoid tracking by network name.
static uint64_t hash_ies(const uint8_t *ie_start, int ie_len)
{
    uint8_t buf[256];
    int     buf_pos = 0;
    int     pos     = 0;

    while (pos + 2 <= ie_len && buf_pos < (int)sizeof(buf) - 3) {
        uint8_t tag = ie_start[pos];
        uint8_t len = ie_start[pos + 1];
        if (pos + 2 + len > ie_len) break;

        if (tag != 0) { // skip SSID
            int copy = len;
            if (buf_pos + 2 + copy > (int)sizeof(buf)) {
                copy = (int)sizeof(buf) - buf_pos - 2;
            }
            if (copy < 0) break;
            buf[buf_pos++] = tag;
            buf[buf_pos++] = len;
            memcpy(&buf[buf_pos], &ie_start[pos + 2], copy);
            buf_pos += copy;
        }
        pos += 2 + len;
    }

    return fnv1a_64(buf, buf_pos);
}

static void IRAM_ATTR promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type == WIFI_PKT_MISC) return;

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;

    // Minimum 802.11 header is 24 bytes
    if (pkt->rx_ctrl.sig_len < 24) return;

    const uint8_t *f = pkt->payload;

    PacketMeta m = {0};
    m.timestamp_us = esp_timer_get_time();
    m.rssi         = pkt->rx_ctrl.rssi;
    m.noise_floor  = pkt->rx_ctrl.noise_floor;
    m.bb_format    = (uint8_t)pkt->rx_ctrl.cur_bb_format;
    m.channel      = pkt->rx_ctrl.channel;
    m.pkt_length   = pkt->rx_ctrl.sig_len;
    m.he_siga1     = pkt->rx_ctrl.he_siga1;
    m.he_siga2     = pkt->rx_ctrl.he_siga2;

    // 802.11 MAC header:
    // f[0]: [1:0]=version, [3:2]=type, [7:4]=subtype
    // f[1]: [0]=to_ds, [1]=from_ds, [2]=more_frag, [3]=retry, ...
    m.frame_type    = (f[0] >> 2) & 0x03;
    m.frame_subtype = (f[0] >> 4) & 0x0F;
    m.retry         = (f[1] >> 3) & 0x01;
    m.seq_ctrl      = (uint16_t)f[22] | ((uint16_t)f[23] << 8);

    // Address 2 (transmitter) at offset 10, Address 3 (BSSID) at offset 16
    memcpy(m.addr_src,   &f[10], 6);
    memcpy(m.addr_bssid, &f[16], 6);

    // Probe Request: Management frame (type=0), subtype=4
    // IEs start at offset 24 (after fixed MAC header)
    if (m.frame_type == 0 && m.frame_subtype == 0x04) {
        int ie_len = (int)pkt->rx_ctrl.sig_len - 24 - 4; // subtract header + FCS
        if (ie_len > 0) {
            m.ie_hash      = hash_ies(&f[24], ie_len);
            m.is_probe_req = 1;
        }
    }

    s_rx_count++;

    // Non-blocking send — drop if queue full (intentional: monitoring > perfect capture)
    if (xQueueSend(s_queue, &m, 0) != pdTRUE) {
        s_drop_count++;
    }
}

esp_err_t wifi_sniffer_init(uint8_t initial_channel)
{
    s_queue = xQueueCreateStatic(SNIFFER_QUEUE_DEPTH, sizeof(PacketMeta),
                                  s_queue_buf, &s_queue_struct);
    if (!s_queue) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_channel(initial_channel, WIFI_SECOND_CHAN_NONE));

    ESP_LOGI(TAG, "Sniffer init on channel %d", initial_channel);
    return ESP_OK;
}

void wifi_sniffer_set_channel(uint8_t channel)
{
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

QueueHandle_t wifi_sniffer_get_queue(void)
{
    return s_queue;
}

void wifi_sniffer_get_stats(uint32_t *received, uint32_t *dropped)
{
    if (received) *received = s_rx_count;
    if (dropped)  *dropped  = s_drop_count;
}
