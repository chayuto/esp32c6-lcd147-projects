# 12 — MCP GPIO

An **ESP32-C6 running a Model Context Protocol (MCP) server** that exposes full GPIO control to any MCP-compatible AI assistant (Claude, etc.) over Wi-Fi.

Configure pins, read voltages, drive PWM, scan I2C buses, and control the onboard RGB LED — all via JSON-RPC 2.0. A live LVGL dashboard on the 1.47" LCD shows every pin's mode and value in real time, colour-coded by state.

No extra hardware required to demo: the onboard WS2812 RGB LED is directly controllable via `set_rgb_led`.

## Architecture

```
AI Client (Claude / Python / MCP host)
          │
          │  HTTP / JSON-RPC 2.0
          ▼
    ESP32-C6 MCP Server  (esp32-gpio.local / port 80)
    ├── POST /mcp         ← tool calls
    └── GET  /ping        ← health check
          │
          ├── gpio_state.c   ← hardware layer (GPIO, LEDC PWM, ADC oneshot)
          ├── led_status.c   ← WS2812 RGB LED (boot indicator + MCP override)
          └── board_config.h ← single file to port to a different board
          │
          │  LVGL 500ms timer
          ▼
    LVGL 8 Table (172×320 portrait)   ← live pin dashboard
          │
          │  SPI2 + DMA
          ▼
    ST7789 1.47" LCD
```

## MCP Tools

| Tool | Description |
|---|---|
| `get_gpio_capabilities` | Returns board name, safe pins with ADC capability and notes, reserved pins, onboard RGB LED info. **Call first.** |
| `configure_pins` | Set one or more pins to INPUT / OUTPUT / ADC / PWM mode |
| `write_digital_pins` | Set logic level (0/1) on OUTPUT pins |
| `read_pins` | Read current value — 0/1 for digital, mV for ADC, % for PWM |
| `set_pwm_duty` | Set PWM duty cycle 0–100% on PWM-mode pins (5kHz, 10-bit) |
| `i2c_scan` | Scan I2C bus on any two safe pins, returns device addresses |
| `set_rgb_led` | Set onboard WS2812 RGB LED colour (r/g/b 0–255). No extra hardware needed. |

7 tools — within the recommended budget. Discovery tool listed first.

## Live Dashboard

The LCD table updates every 500ms:

| Row colour | Meaning |
|---|---|
| Green | OUTPUT HIGH |
| Red | OUTPUT LOW |
| Amber | INPUT |
| Blue | ADC |
| Purple | PWM |
| Dark grey | Unconfigured |

## HTTP Endpoints

| Method | Path | Description |
|---|---|---|
| `POST` | `/mcp` | JSON-RPC 2.0 — `initialize`, `tools/list`, `tools/call` |
| `GET` | `/ping` | `{"status":"healthy","server":"esp32-gpio"}` |

mDNS: device advertises as `esp32-gpio.local` — no IP lookup needed on the same network.

## Board Portability

`main/board_config.h` is the **only file you need to edit** to port this project to a different ESP32 board. It defines:

- `SAFE_DIGITAL_PINS[]` — struct array of `{gpio, note}` pairs. The `note` field is returned by `get_gpio_capabilities` so the LLM knows if a pin is wired to something onboard.
- `ADC_CAPABLE_PINS[]` / `ADC_CHANNELS[]` — subset of safe pins with ADC1 channel mapping
- `BOARD_RGB_LED_GPIO` — GPIO of the onboard WS2812, or `-1` if absent
- `g_reserved_pins[]` — pins the LLM must not touch (LCD, SD, USB, strapping)

## Configuration

Copy the template and fill in credentials:

```zsh
cp projects/12_mcp_gpio/sdkconfig.defaults.template \
   projects/12_mcp_gpio/sdkconfig.defaults
# Edit CONFIG_GPIO_WIFI_SSID and CONFIG_GPIO_WIFI_PASSWORD
```

## Build & Flash

```zsh
. ~/esp/esp-idf/export.sh

idf.py -C projects/12_mcp_gpio set-target esp32c6   # first time only
idf.py -C projects/12_mcp_gpio build
idf.py -C projects/12_mcp_gpio -p /dev/cu.usbmodem1101 flash
```

Binary: ~1.3MB in 2MB app partition (~38% headroom).

## Testing

An integration test script covers all 7 tools and error handling:

```zsh
python3 -m venv /tmp/mcp_venv && /tmp/mcp_venv/bin/pip install requests

/tmp/mcp_venv/bin/python3 projects/12_mcp_gpio/test_mcp.py <device-ip>
```

Tests: health check, MCP handshake, capabilities schema, configure + read, write digital, PWM, I2C scan (no device = expected empty), RGB LED, error handling.

## Project Structure

```
12_mcp_gpio/
├── main/
│   ├── main.c              # Boot: NVS, LCD, gpio_state, LED, LVGL, Wi-Fi, MCP
│   ├── board_config.h      # ONLY file to edit when porting to another board
│   ├── gpio_state.c/.h     # Hardware layer: GPIO, LEDC PWM, ADC oneshot, mutex
│   ├── mcp_tools.c/.h      # 7 tool handlers + cached tools/list JSON
│   ├── mcp_server.c/.h     # Wi-Fi STA, HTTP server, JSON-RPC 2.0 router
│   ├── ui_display.c/.h     # LVGL table dashboard, 500ms refresh timer
│   ├── led_status.c/.h     # WS2812 LED: connecting (amber blink), ready (green), error (red), MCP override
│   ├── idf_component.yml   # Managed deps: mdns >=1.0.0
│   └── Kconfig.projbuild   # GPIO_WIFI_SSID / GPIO_WIFI_PASSWORD config symbols
├── test_mcp.py             # Python integration tests
├── sdkconfig.defaults.template
└── partitions.csv          # 2MB app partition
```

## Technical Notes

**GPIO safety**

Four-layer protection prevents the LLM from requesting unsafe pins:
1. JSON schema `enum` restricts gpio parameters to `SAFE_DIGITAL_PINS` values
2. `get_gpio_capabilities` returns reserved pins with reasons (so the LLM understands why)
3. Tool descriptions include negative guidance ("Do NOT configure reserved pins")
4. Firmware enforces `board_is_safe_pin()` — bad requests return a tool error, never a hardware write

**PWM**

Uses `LEDC_TIMER_1` at 5kHz, 10-bit resolution. `LEDC_TIMER_0` is reserved by the LCD backlight driver. Up to 8 simultaneous PWM channels. Reconfiguring a pin away from PWM automatically stops the LEDC channel and frees the slot.

**ADC**

Single `adc_oneshot` unit (ADC1 only — ESP32-C6 has no ADC2). Curve-fitting calibration enabled when eFuse data is present; falls back to raw counts otherwise. All ADC reads return millivolts.

**WS2812 LED**

R and G channels are physically swapped on the Waveshare board's WS2812. The `set_pixel()` wrapper in `led_status.c` corrects this by passing `led_strip_set_pixel(strip, 0, g, r, b)`.

**LVGL thread safety**

`lv_timer_create()` for the 500ms dashboard refresh is called in `ui_display_init()`, before the LVGL task starts. Calling it from `app_main` after the LVGL task is running risks corrupting the timer linked list on a tick boundary (single-core FreeRTOS, same priority).
