#include "airtime.h"
#include "esp_wifi_he_types.h"

#include <math.h>
#include <stdint.h>

// N_DBPS: data bits per OFDM symbol

// 802.11ax HE 20 MHz 1SS, MCS 0–11
static const uint16_t k_he_ndbps[12] = {
    117, 234, 351, 468, 702, 936, 1053, 1170, 1404, 1560, 1755, 1950
};

// 802.11n HT 20 MHz 1SS, MCS 0–7
static const uint16_t k_ht_ndbps[8] = {
    26, 52, 78, 104, 156, 208, 234, 260
};

// General OFDM air time formula (all standards except 802.11b)
// T_air = T_preamble + ceil((16 + 8*payload + 6) / N_DBPS) * T_sym
static double calc_ofdm(double t_pre, double t_sym, uint16_t n_dbps, uint16_t payload)
{
    if (n_dbps == 0) return t_pre + 1000.0; // fallback 1 ms
    double n_sym = ceil((16.0 + 8.0 * payload + 6.0) / (double)n_dbps);
    return t_pre + n_sym * t_sym;
}

double estimate_airtime_us(const PacketMeta *m)
{
    uint16_t payload = m->pkt_length;
    if (payload == 0) return 20.0; // guard: minimum plausible frame

    switch ((wifi_rx_bb_format_t)m->bb_format) {

    case RX_BB_FORMAT_11B:
        // Short preamble (96 µs) + payload at assumed 11 Mbps
        return 96.0 + ceil(8.0 * payload / 11.0);

    case RX_BB_FORMAT_11G:
        // 802.11a/g OFDM: 20 µs preamble + 6 µs signal extension = 26 µs, 4.0 µs/symbol
        // Assume 54 Mbps (N_DBPS=216) — upper-bound estimate
        return calc_ofdm(26.0, 4.0, 216, payload);

    case RX_BB_FORMAT_HT: {
        // he_siga1 holds HT-SIG for HT frames
        uint8_t mcs = (uint8_t)(m->he_siga1 & 0x7F); // bits [6:0]
        uint8_t sgi = (uint8_t)((m->he_siga1 >> 7) & 0x01);
        if (mcs > 7) mcs = 7;
        double t_sym = sgi ? 3.6 : 4.0;
        return calc_ofdm(36.0, t_sym, k_ht_ndbps[mcs], payload);
    }

    case RX_BB_FORMAT_VHT:
        // VHT rare on 2.4 GHz; treat as HT MCS 7
        return calc_ofdm(36.0, 4.0, k_ht_ndbps[7], payload);

    case RX_BB_FORMAT_HE_SU:
    case RX_BB_FORMAT_HE_ERSU:
    case RX_BB_FORMAT_HE_TB: {
        // MCS from he_siga1 bits [3:0]
        uint8_t mcs    = (uint8_t)(m->he_siga1 & 0x0F);
        // GI+LTF from he_siga2 bits [13:12]: 3 → 3.2 µs GI → T_sym=16.0 µs
        uint8_t gi_ltf = (uint8_t)((m->he_siga2 >> 12) & 0x03);
        double  t_sym  = (gi_ltf == 3) ? 16.0 : 13.6;
        if (mcs > 11) mcs = 11;
        return calc_ofdm(44.0, t_sym, k_he_ndbps[mcs], payload);
    }

    case RX_BB_FORMAT_HE_MU:
        // MU payload shared across users; estimate using MCS 7 HE
        return calc_ofdm(52.0, 13.6, k_he_ndbps[7], payload);

    default:
        // Unknown format: assume 24 Mbps OFDM
        return calc_ofdm(26.0, 4.0, 96, payload);
    }
}
