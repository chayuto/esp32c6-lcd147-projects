# ESP32-C6 Hardware Specs & Limitations

Reference for designing features on the ESP32-C6 + 1.47" ST7789 board.
Use this before writing any code that touches peripherals, memory, or wireless.

---

## CPU

- **Core:** Single RISC-V at 160MHz
- **No FPU** — software float is ~10-20× slower than hardware. Audit all `float`/`double` usage.
- **No SIMD** — SIMD (PIE) exists only on ESP32-S3. Libraries claiming SIMD acceleration don't use it on C6.
- **Single core** — `xTaskCreatePinnedToCore()` compiles but is wrong. Use `xTaskCreate()`.

## Memory

- **SRAM:** 512KB total, ~400KB available after OS/ROM overhead
- **Flash:** 8MB embedded (FH8 variant on our board)
- **No PSRAM** on our board — everything must fit in 512KB SRAM
- **Canvas framebuffer:** 172×320×2 = 110,080 bytes (107KB) — must be static BSS, not heap
- **Heap fragmentation risk:** prefer static allocation (`StaticQueue_t`, `StaticTask_t`) for long-lived objects

## Hardware Accelerators — What Exists

| Accelerator | Details | mbedtls auto-uses? |
|---|---|---|
| AES | 128/256-bit, DMA-capable, XTS for flash | ✓ yes (CONFIG_MBEDTLS_HARDWARE_AES) |
| SHA | SHA-1/224/256/384/512, DMA-capable | ✓ yes (CONFIG_MBEDTLS_HARDWARE_SHA) |
| RSA | Up to 3072-bit | ✓ yes (used for TLS) |
| ECC | Elliptic curve, ECDSA | ✓ yes |
| HMAC | Dedicated hardware HMAC peripheral | Manual (`esp_hmac.h`) |
| RNG | True hardware RNG | ✓ automatic |
| Digital Signature | Hardware DS for identity protection | Manual |

**mbedtls uses AES and SHA hardware automatically** when `CONFIG_MBEDTLS_HARDWARE_AES=y` and
`CONFIG_MBEDTLS_HARDWARE_SHA=y` (both are default). If you use `mbedtls_sha256()` or TLS,
you're already getting hardware acceleration.

**Base64 has no hardware path** — `mbedtls_base64_encode()` is always software. Fast enough (<1ms for 10KB).

## Hardware Accelerators — What Does NOT Exist

| Feature | Reality |
|---|---|
| **Hardware JPEG encoder/decoder** | **P4-only.** Use `espressif/esp_new_jpeg` (software, supports C6 encoding) |
| **Hardware H.264/video codec** | Not present |
| **Hardware FPU** | Not present — software float only |
| **SIMD/vector instructions** | Not present — S3-only |
| **PSRAM interface** | Not present on our board |

## Peripherals with DMA

SPI2, UHCI0, I2S, AES, SHA, ADC, PARLIO.
Everything else (I2C, UART, GPIO) is CPU-driven.

## Display (ST7789 via SPI2)

- **Resolution:** 172×320 portrait, RGB565
- **SPI clock:** 12MHz (safe; max per datasheet ~15MHz — don't increase without signal integrity check)
- **DMA-driven:** `esp_lcd_panel` uses SPI2+DMA. CPU is free during pixel flush.
- **LVGL draw buffer:** 172×20×2 = 6,880 bytes (20 rows). Each full-screen redraw = 16 DMA transactions.
- **Offset:** X+34, Y+0 (ST7789 RAM addressing offset — baked into shared driver)

## Wi-Fi

- **Standard:** Wi-Fi 6 (802.11ax) — but IDF STA mode uses basic 802.11 by default
- **Modem sleep is ON by default** (`WIFI_PS_MIN_MODEM`) — adds DTIM-interval latency (100–300ms) to every incoming TCP connection. **For HTTP servers: always call `esp_wifi_set_ps(WIFI_PS_NONE)` after connecting.**
- **Wi-Fi 6 features available but unused in basic STA:** TWT (Target Wake Time), OFDMA, MU-MIMO, BSS Coloring — irrelevant for HTTP server use case
- **BLE 5:** Available. Disable with `CONFIG_BT_ENABLED=n` to save ~50KB flash + ~30KB RAM.
- **Thread/Zigbee (802.15.4):** Available. Not useful for Wi-Fi HTTP projects.

## Software JPEG on ESP32-C6

Use `espressif/esp_new_jpeg` v0.6.1 from the IDF Component Registry:
- Supports **encoding on C6** (pure software, no hardware)
- Add to `idf_component.yml`: `espressif/esp_new_jpeg: "~0.6.1"`
- API: `jpeg_enc_open()` → `jpeg_enc_process()` → `jpeg_enc_close()`
- Input: RGB888 (convert from RGB565 first)
- **Downscale 2× before encoding** to keep RGB888 intermediate buffer manageable:
  - Full res RGB888: 172×320×3 = 165KB (OOM risk)
  - Half res RGB888: 86×160×3 = 41KB (safe, fits in BSS)
- Estimated encode time: 50–80ms for 86×160 at quality 35

## Typical RAM Budget for LVGL + Wi-Fi Projects

```
Canvas buffer (BSS):       110,080 B  (172×320 RGB565)
RGB888 snapshot buf (BSS):  41,280 B  (86×160 — only if snapshot feature needed)
LVGL heap pool:             32,768 B  (CONFIG_LV_MEM_SIZE_KILOBYTES=32)
LVGL draw buffer:            6,880 B  (172×20 RGB565)
Wi-Fi STA stack:            ~55,000 B
FreeRTOS tasks (typical):   ~40,000 B
HTTP server:                 ~8,000 B
Misc heap (cJSON, etc.):    ~10,000 B
---
Total:                     ~304 KB of 512 KB
Headroom:                  ~208 KB (but contiguous heap may be less)
```

`CONFIG_LV_MEM_SIZE_KILOBYTES=32` is sufficient for canvas-based projects (canvas uses static BSS).
Use 64KB only if you have complex widget trees (like 01_ntp_clock).

## Gotchas Confirmed in Production

- **Wi-Fi modem sleep:** Default ON. Always `esp_wifi_set_ps(WIFI_PS_NONE)` for HTTP servers.
- **No hardware JPEG:** `esp_jpeg` IDF component = hardware P4 only. Use `esp_new_jpeg` component instead.
- **No FPU:** Avoid float in hot paths. Use fixed-point or integer math.
- **No SIMD:** Ignore claims of SIMD acceleration — they apply to S3, not C6.
- **SPI2 shared:** LCD and SD card share SPI2. `spi_bus_initialize()` returns `ESP_ERR_INVALID_STATE` on second init — check and skip as non-fatal.
- **`xTaskCreate` not pinned:** Single-core chip. `xTaskCreatePinnedToCore()` is wrong API.
- **Canvas buf in BSS:** Declare as `static lv_color_t g_canvas_buf[172*320]` — never `malloc()` for this.
- **esp_task_wdt:** Not a standalone component in IDF 5.5. Use `esp_system` in CMakeLists REQUIRES.
- **LVGL mutations from FreeRTOS tasks:** Crashes before first render. All `lv_*` calls must be inside `lv_timer` callbacks only.
