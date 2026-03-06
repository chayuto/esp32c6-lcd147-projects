#include "channel_util.h"

#include <string.h>
#include <stdint.h>

#define CU_WINDOW_US  1000000LL  // 1 second
#define CU_BUF_SIZE   1024

typedef struct {
    int64_t ts;
    double  airtime;
} cu_entry_t;

static cu_entry_t s_buf[CU_BUF_SIZE];
static int     s_head  = 0;
static int     s_tail  = 0;
static int     s_count = 0;
static double  s_sum   = 0.0;

static void evict_expired(int64_t now_us)
{
    while (s_count > 0 && s_buf[s_tail].ts + CU_WINDOW_US < now_us) {
        s_sum -= s_buf[s_tail].airtime;
        s_tail = (s_tail + 1) % CU_BUF_SIZE;
        s_count--;
    }
    if (s_sum < 0.0) s_sum = 0.0; // guard floating-point drift
}

double cu_add(int64_t now_us, double airtime_us)
{
    evict_expired(now_us);

    if (s_count < CU_BUF_SIZE) {
        s_buf[s_head].ts      = now_us;
        s_buf[s_head].airtime = airtime_us;
        s_sum += airtime_us;
        s_head = (s_head + 1) % CU_BUF_SIZE;
        s_count++;
    }
    // If full and not expired: oldest entry is <1s ago — drop newest silently

    double pct = (s_sum / (double)CU_WINDOW_US) * 100.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

double cu_get_pct(void)
{
    double pct = (s_sum / (double)CU_WINDOW_US) * 100.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

uint32_t cu_get_pkt_per_sec(void)
{
    return (uint32_t)s_count;
}

void cu_reset(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head  = 0;
    s_tail  = 0;
    s_count = 0;
    s_sum   = 0.0;
}
