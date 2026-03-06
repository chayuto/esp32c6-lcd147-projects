# 11 — MCP Server Display

An **ESP32-C6 running a Model Context Protocol (MCP) server** — letting any MCP-compatible AI assistant (Claude, etc.) draw shapes, text, and graphics on a physical 1.47" LCD over Wi-Fi.

The AI interacts with the device exactly like any other MCP tool: JSON-RPC 2.0 calls to draw primitives. The device responds with a JPEG snapshot of the current screen so the AI can see what it drew and self-correct — closing the visual feedback loop on bare metal.

## Architecture

```
AI Client (Claude / Python)
          │
          │  HTTP / JSON-RPC 2.0
          ▼
    ESP32-C6 MCP Server
    ├── POST /mcp          ← tool calls (draw, snapshot, info)
    ├── GET  /ping         ← health check + queue depth
    └── GET  /snapshot.jpg ← raw JPEG binary
          │
          │  FreeRTOS draw queue (StaticQueue, depth 8)
          ▼
    LVGL 8 Canvas (172×320 RGB565, static BSS)
          │
          │  SPI2 + DMA
          ▼
    ST7789 1.47" LCD
```

## MCP Tools

| Tool | Description |
|---|---|
| `get_canvas_info` | Returns screen dimensions, coordinate ranges, font sizes. **Call first.** |
| `clear_canvas` | Fills the entire canvas with a solid color |
| `draw_rect` | Filled or outlined rectangle with optional corner radius |
| `draw_line` | Line between two points with configurable width |
| `draw_arc` | Arc segment with start/end angles and line width |
| `draw_text` | Text at a position with RGB color and font size (14 / 18 / 24) |
| `draw_path` | Polyline or filled/closed polygon, up to 8 points |
| `get_canvas_snapshot` | Base64-encoded JPEG of the current screen (86×160, 2× downsample) |

8 tools — within the recommended budget. Discovery tool is listed first per MCP best practice.

## HTTP Endpoints

| Method | Path | Description |
|---|---|---|
| `POST` | `/mcp` | JSON-RPC 2.0 — `initialize`, `tools/list`, `tools/call` |
| `GET` | `/ping` | `{"status":"healthy","queue_depth":N,"queue_max":8}` |
| `GET` | `/snapshot.jpg` | Raw JPEG binary (86×160 px, max 20KB) |

mDNS: device advertises as `esp32-canvas.local` — no IP lookup needed on the same network.

## Configuration

Copy the template and fill in credentials:

```zsh
cp projects/11_mcp_server_display/sdkconfig.defaults.template \
   projects/11_mcp_server_display/sdkconfig.defaults
# Edit CONFIG_CANVAS_WIFI_SSID and CONFIG_CANVAS_WIFI_PASSWORD
```

## Build & Flash

```zsh
. ~/esp/esp-idf/export.sh

idf.py -C projects/11_mcp_server_display set-target esp32c6   # first time only
idf.py -C projects/11_mcp_server_display build
idf.py -C projects/11_mcp_server_display -p /dev/cu.usbmodem1101 flash
```

Binary: ~1.3MB in 2MB app partition (~38% headroom).

## Testing

An integration test suite covers all tools and endpoints:

```zsh
python3 -m venv /tmp/mcp_venv && /tmp/mcp_venv/bin/pip install requests

# Run all 8 test suites
/tmp/mcp_venv/bin/python3 test_mcp.py <device-ip>

# Save JPEG snapshots after each test
/tmp/mcp_venv/bin/python3 test_mcp.py <device-ip> --save-snapshots
```

Suites: health check, MCP handshake, tools/list (8 tools verified), canvas info, drawing primitives (all 6 types), error handling (6 bad-arg cases), snapshot (MCP + HTTP paths), showcase HUD scene.

The showcase draws a sci-fi radar HUD using every primitive — concentric range rings, crosshair grid, sweep arc with leading-edge radial, radar blips, status bars (SIG/PWR/UPL), and a "DRAWN VIA MCP" footer. All 8 suites pass.

## Project Structure

```
11_mcp_server_display/
├── main/
│   ├── main.c              # Boot: NVS, LCD init, LVGL task, Wi-Fi, MCP start
│   ├── mcp_server.c/.h     # Wi-Fi STA, HTTP server, JSON-RPC router, 8 tool handlers
│   ├── drawing_engine.c/.h # draw_cmd_t union, StaticQueue, drawing_push_* helpers
│   ├── ui_display.c/.h     # LVGL canvas init, 50ms render timer, canvas buf + mutex
│   ├── snapshot.c/.h       # RGB565→RGB888 downsample, esp_new_jpeg encode, base64
│   ├── idf_component.yml   # Managed deps: esp_new_jpeg ~0.6.1, mdns >=1.0.0
│   └── Kconfig.projbuild   # CANVAS_WIFI_SSID / CANVAS_WIFI_PASSWORD config symbols
├── test_mcp.py             # Python integration tests + showcase scene
├── sdkconfig.defaults.template
└── partitions.csv          # 2MB app partition
```

## Technical Notes

**JPEG snapshot pipeline**

LVGL's canvas is RGB565 (little-endian). The `esp_new_jpeg` software encoder rejects RGB565 — it only accepts RGB888, RGBA, YCbYCr, and GRAY. During the 2× downsample pass (held under canvas mutex), each pixel is converted inline from RGB565 to RGB888. The encoder uses `JPEG_SUBSAMPLE_444` (no dimension alignment required — 86px wide would fail the 16-wide constraint of 4:2:0). Both the 41KB RGB888 intermediate buffer and the 20KB JPEG output buffer are declared as static BSS — no heap allocation.

**LVGL thread safety**

All `lv_*` mutations are confined to the 50ms render timer running inside the LVGL task. The MCP HTTP handler (a separate task) pushes `draw_cmd_t` structs onto `g_draw_queue` (non-blocking, depth 8). The render timer drains the queue and executes draws on the canvas. `g_canvas_mutex` (binary semaphore) gates canvas buffer reads by the snapshot handler to prevent tearing.

**Wi-Fi latency**

`esp_wifi_set_ps(WIFI_PS_NONE)` is called after GOT_IP. Without this, the default modem sleep (DTIM interval) adds 100–300ms latency to every incoming HTTP connection.
