#include "device_counter.h"

#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/md.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "dc";

#define OBS_TABLE_SIZE  256
#define OBS_MAX_AGE_US  (60LL * 1000000LL)
#define SIG_SET_SIZE    512
#define SALT_ROTATE_US  (3600LL * 1000000LL) // 1 hour

typedef struct {
    uint64_t ie_hash;
    uint64_t device_sig;
    uint16_t last_sn;
    int8_t   last_rssi;
    int64_t  last_seen_us;
} obs_entry_t;

static obs_entry_t s_obs[OBS_TABLE_SIZE];
static uint64_t    s_sig_set[SIG_SET_SIZE]; // 0 = empty
static uint32_t    s_device_count = 0;

static uint8_t  s_salt[32];
static int64_t  s_salt_gen_us = 0;

static void generate_salt(void)
{
    for (int i = 0; i < 8; i++) {
        uint32_t r = esp_random();
        memcpy(&s_salt[i * 4], &r, 4);
    }
}

static void maybe_rotate_salt(int64_t now_us)
{
    if (now_us - s_salt_gen_us >= SALT_ROTATE_US) {
        generate_salt();
        s_salt_gen_us = now_us;
        memset(s_sig_set, 0, sizeof(s_sig_set));
        memset(s_obs,     0, sizeof(s_obs));
        s_device_count = 0;
        ESP_LOGI(TAG, "Salt rotated — device count reset");
    }
}

static uint64_t compute_device_sig(uint64_t ie_hash, uint16_t sn, int8_t rssi)
{
    // Quantise to reduce sensitivity to noise
    uint8_t  rssi_bucket  = (uint8_t)((rssi / 5) * 5 + 128);
    uint16_t sn_interval  = sn / 10;

    uint8_t input[11];
    memcpy(input,     &ie_hash,    8);
    memcpy(input + 8, &sn_interval, 2);
    input[10] = rssi_bucket;

    uint8_t hmac_out[32];
    mbedtls_md_hmac(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
        s_salt, sizeof(s_salt),
        input, sizeof(input),
        hmac_out
    );

    uint64_t sig;
    memcpy(&sig, hmac_out, sizeof(sig));
    return sig;
}

static void sig_set_insert(uint64_t sig)
{
    if (sig == 0) sig = 1; // avoid sentinel
    uint32_t slot = (uint32_t)(sig % SIG_SET_SIZE);
    for (int i = 0; i < SIG_SET_SIZE; i++) {
        uint32_t idx = (slot + i) % SIG_SET_SIZE;
        if (s_sig_set[idx] == 0) {
            s_sig_set[idx] = sig;
            s_device_count++;
            return;
        }
        if (s_sig_set[idx] == sig) return; // already counted
    }
    // Table full — overcount slightly, acceptable for density estimation
    s_device_count++;
}

void device_counter_init(void)
{
    generate_salt();
    s_salt_gen_us  = esp_timer_get_time();
    s_device_count = 0;
    memset(s_obs,     0, sizeof(s_obs));
    memset(s_sig_set, 0, sizeof(s_sig_set));
}

void device_counter_add(const PacketMeta *m, int64_t now_us)
{
    if (!m->is_probe_req) return;

    maybe_rotate_salt(now_us);

    uint64_t ie_hash = m->ie_hash;
    uint16_t sn      = (m->seq_ctrl >> 4) & 0x0FFF; // 12-bit SN
    int8_t   rssi    = m->rssi;

    int match_idx  = -1;
    int empty_idx  = -1;
    int oldest_idx = 0;
    int64_t oldest = INT64_MAX;

    for (int i = 0; i < OBS_TABLE_SIZE; i++) {
        obs_entry_t *e = &s_obs[i];

        if (e->last_seen_us == 0) {
            if (empty_idx < 0) empty_idx = i;
            continue;
        }
        // Evict stale
        if (now_us - e->last_seen_us > OBS_MAX_AGE_US) {
            memset(e, 0, sizeof(*e));
            if (empty_idx < 0) empty_idx = i;
            continue;
        }
        if (e->last_seen_us < oldest) {
            oldest     = e->last_seen_us;
            oldest_idx = i;
        }

        if (e->ie_hash != ie_hash) continue;

        // SN continuity: gap < 256 within 30 s
        if (now_us - e->last_seen_us > 30LL * 1000000LL) continue;
        int sn_diff = (int)sn - (int)e->last_sn;
        if (sn_diff < 0) sn_diff += 4096; // 12-bit rollover
        if (sn_diff >= 256) continue;

        // RSSI proximity: within 15 dBm
        int rdiff = (int)rssi - (int)e->last_rssi;
        if (rdiff < 0) rdiff = -rdiff;
        if (rdiff >= 15) continue;

        match_idx = i;
        break;
    }

    if (match_idx >= 0) {
        // Known device — update observation, do not increment count
        s_obs[match_idx].last_sn      = sn;
        s_obs[match_idx].last_rssi    = rssi;
        s_obs[match_idx].last_seen_us = now_us;
    } else {
        // New device
        int slot = (empty_idx >= 0) ? empty_idx : oldest_idx;
        s_obs[slot].ie_hash      = ie_hash;
        s_obs[slot].last_sn      = sn;
        s_obs[slot].last_rssi    = rssi;
        s_obs[slot].last_seen_us = now_us;
        s_obs[slot].device_sig   = compute_device_sig(ie_hash, sn, rssi);
        sig_set_insert(s_obs[slot].device_sig);
    }
}

uint32_t device_counter_get_count(void)
{
    return s_device_count;
}

void device_counter_reset(void)
{
    memset(s_obs,     0, sizeof(s_obs));
    memset(s_sig_set, 0, sizeof(s_sig_set));
    s_device_count = 0;
    // Salt preserved across channel changes intentionally
}
