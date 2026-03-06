#include "snapshot.h"
#include "ui_display.h"
#include "drawing_engine.h"
#include "esp_jpeg_enc.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "snapshot";

#define SNAP_W         86          // SCREEN_W / 2
#define SNAP_H         160         // SCREEN_H / 2
#define JPEG_BUF_SIZE  20480       // 20KB ceiling for 86x160 RGB888 at quality 35

// Static BSS — no heap allocation for these hot buffers.
// esp_new_jpeg encoder does NOT support RGB565 input; RGB888 required.
// We convert during the downsampled canvas copy (while holding canvas mutex)
// so no intermediate RGB565 buffer is needed.
static uint8_t s_rgb888_buf[SNAP_W * SNAP_H * 3]; // 41,280 B — RGB888 to encode
static uint8_t s_jpeg_out[JPEG_BUF_SIZE];          // 20,480 B — JPEG output

// Extract 8-bit channels from an LVGL lv_color_t (RGB565 little-endian).
// lv_color16_t bitfield: blue[4:0] green[10:5] red[15:11]
static inline void lv_color_to_rgb888(lv_color_t c,
                                      uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = c.ch.red;
    uint8_t g6 = c.ch.green;
    uint8_t b5 = c.ch.blue;
    // Scale 5-bit → 8-bit: replicate top bits into low bits
    *r = (r5 << 3) | (r5 >> 2);
    *g = (g6 << 2) | (g6 >> 4);
    *b = (b5 << 3) | (b5 >> 2);
}

static bool do_encode(int *jpeg_len_out)
{
    // 1. Lock canvas, 2x downsample + RGB565→RGB888 conversion in one pass
    if (xSemaphoreTake(g_canvas_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "Canvas mutex timeout");
        return false;
    }
    for (int y = 0; y < SNAP_H; y++) {
        for (int x = 0; x < SNAP_W; x++) {
            lv_color_t px = g_canvas_buf[(y * 2) * SCREEN_W + (x * 2)];
            uint8_t *dst = &s_rgb888_buf[(y * SNAP_W + x) * 3];
            lv_color_to_rgb888(px, &dst[0], &dst[1], &dst[2]);
        }
    }
    xSemaphoreGive(g_canvas_mutex);

    // 2. JPEG encode — RGB888, 4:4:4 subsampling (no dimension alignment needed)
    jpeg_enc_config_t enc_cfg = {
        .width       = SNAP_W,
        .height      = SNAP_H,
        .src_type    = JPEG_PIXEL_FORMAT_RGB888,
        .subsampling = JPEG_SUBSAMPLE_444,
        .quality     = 35,
        .rotate      = JPEG_ROTATE_0D,
        .task_enable = false,
    };

    jpeg_enc_handle_t handle;
    if (jpeg_enc_open(&enc_cfg, &handle) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_enc_open failed");
        return false;
    }

    int jpeg_len = 0;
    jpeg_error_t ret = jpeg_enc_process(handle,
                                        s_rgb888_buf,
                                        sizeof(s_rgb888_buf),
                                        s_jpeg_out,
                                        JPEG_BUF_SIZE,
                                        &jpeg_len);
    jpeg_enc_close(handle);

    if (ret != JPEG_ERR_OK || jpeg_len <= 0) {
        ESP_LOGE(TAG, "jpeg_enc_process failed: %d", ret);
        return false;
    }

    ESP_LOGI(TAG, "JPEG encoded: %d bytes", jpeg_len);
    *jpeg_len_out = jpeg_len;
    return true;
}

bool snapshot_encode_raw(uint8_t *out_buf, size_t out_cap, int *jpeg_len)
{
    int len = 0;
    if (!do_encode(&len)) return false;
    if ((size_t)len > out_cap) {
        ESP_LOGE(TAG, "Output buffer too small: need %d, have %zu", len, out_cap);
        return false;
    }
    memcpy(out_buf, s_jpeg_out, len);
    *jpeg_len = len;
    return true;
}

bool snapshot_encode(unsigned char **b64_out, size_t *b64_len_out)
{
    int jpeg_len = 0;
    if (!do_encode(&jpeg_len)) return false;

    // Base64 encode — output is heap-allocated, caller frees
    size_t b64_cap = ((jpeg_len + 2) / 3) * 4 + 4;
    unsigned char *b64 = malloc(b64_cap);
    if (!b64) {
        ESP_LOGE(TAG, "OOM for base64 buffer (%zu bytes)", b64_cap);
        return false;
    }

    size_t b64_len = 0;
    int rc = mbedtls_base64_encode(b64, b64_cap, &b64_len,
                                   s_jpeg_out, (size_t)jpeg_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "base64 encode failed: %d", rc);
        free(b64);
        return false;
    }

    ESP_LOGI(TAG, "base64 encoded: %zu bytes", b64_len);
    *b64_out     = b64;
    *b64_len_out = b64_len;
    return true;
}
