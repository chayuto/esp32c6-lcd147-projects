#include "rf_metrics.h"

#include <math.h>
#include <string.h>
#include <stdint.h>

#define SNR_ALPHA        0.05
#define NOISE_ALPHA      0.02
#define BSSID_TABLE_SIZE 128
#define BSSID_MAX_AGE_US (60LL * 1000000LL)

// Internal state
static double   s_avg_snr      = 25.0;
static double   s_avg_noise    = -95.0;
static double   s_retry_pct    = 0.0;
static uint32_t s_total_1s     = 0;
static uint32_t s_retry_1s     = 0;
static int64_t  s_window_start = 0;
static double   s_rf_score     = 100.0;

typedef struct {
    uint8_t mac[6];
    int64_t last_seen_us;
} bssid_entry_t;

static bssid_entry_t s_bssids[BSSID_TABLE_SIZE];
static uint32_t      s_bssid_count = 0;

static bool bssid_valid(const uint8_t *mac)
{
    if (mac[0] == 0xFF) return false; // broadcast
    // all-zero check
    for (int i = 0; i < 6; i++) if (mac[i]) return true;
    return false;
}

static void bssid_update(const uint8_t *mac, int64_t now_us)
{
    if (!bssid_valid(mac)) return;

    int     oldest_idx = 0;
    int64_t oldest_ts  = INT64_MAX;

    for (int i = 0; i < BSSID_TABLE_SIZE; i++) {
        if (memcmp(s_bssids[i].mac, mac, 6) == 0) {
            s_bssids[i].last_seen_us = now_us;
            goto recount;
        }
        if (s_bssids[i].last_seen_us < oldest_ts) {
            oldest_ts  = s_bssids[i].last_seen_us;
            oldest_idx = i;
        }
    }
    // New BSSID: replace oldest slot
    memcpy(s_bssids[oldest_idx].mac, mac, 6);
    s_bssids[oldest_idx].last_seen_us = now_us;

recount:;
    uint32_t count = 0;
    for (int i = 0; i < BSSID_TABLE_SIZE; i++) {
        if (s_bssids[i].last_seen_us > 0 &&
            now_us - s_bssids[i].last_seen_us < BSSID_MAX_AGE_US) {
            count++;
        }
    }
    s_bssid_count = count;
}

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static double compute_score(double cu_pct)
{
    double s_cu  = clampd(100.0 - cu_pct,        0, 100);
    double s_snr = clampd(s_avg_snr * 2.5,        0, 100);
    double s_ret = clampd(100.0 - s_retry_pct,    0, 100);
    double s_bss = clampd(100.0 - s_bssid_count * 3.33, 0, 100);
    return 0.40 * s_cu + 0.30 * s_snr + 0.20 * s_ret + 0.10 * s_bss;
}

void rf_metrics_update(const PacketMeta *m, int64_t now_us, double cu_pct)
{
    // 1. SNR EMA
    double snr   = (double)m->rssi - (double)m->noise_floor;
    s_avg_snr    = s_avg_snr  * (1.0 - SNR_ALPHA)   + snr * SNR_ALPHA;
    s_avg_noise  = s_avg_noise * (1.0 - NOISE_ALPHA) + (double)m->noise_floor * NOISE_ALPHA;

    // 2. Retry ratio (management + data frames only)
    if (m->frame_type == 0 || m->frame_type == 2) {
        if (s_window_start == 0) s_window_start = now_us;
        s_total_1s++;
        if (m->retry) s_retry_1s++;

        if (now_us - s_window_start >= 1000000LL) {
            s_retry_pct    = (s_total_1s > 0) ? (s_retry_1s * 100.0 / s_total_1s) : 0.0;
            s_total_1s     = 0;
            s_retry_1s     = 0;
            s_window_start = now_us;
        }
    }

    // 3. BSSID density
    bssid_update(m->addr_bssid, now_us);

    // 4. Composite score
    s_rf_score = compute_score(cu_pct);
}

rf_metrics_snapshot_t rf_metrics_get_snapshot(void)
{
    rf_metrics_snapshot_t snap;
    snap.avg_snr_db     = s_avg_snr;
    snap.avg_noise_floor = (int8_t)s_avg_noise;
    snap.retry_pct      = s_retry_pct;
    snap.bssid_count    = s_bssid_count;
    snap.rf_score       = s_rf_score;
    return snap;
}

void rf_metrics_reset(void)
{
    s_avg_snr      = 25.0;
    s_avg_noise    = -95.0;
    s_retry_pct    = 0.0;
    s_total_1s     = 0;
    s_retry_1s     = 0;
    s_window_start = 0;
    s_rf_score     = 100.0;
    s_bssid_count  = 0;
    memset(s_bssids, 0, sizeof(s_bssids));
}
