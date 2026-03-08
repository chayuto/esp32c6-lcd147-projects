# esp32c6-lcd147-projects

A collection of ESP-IDF firmware projects for the **ESP32-C6 with 1.47" ST7789 LCD** board.

Built with an **agentic-first development workflow** — each project is developed with [Claude Code](https://claude.ai/code) as the primary agent. Hardware specs, build rules, LVGL gotchas, and lessons learned across all projects live in `CLAUDE.md` and `.claude/commands/`, giving the agent full context from the first message of every session.

---

## Board

| Feature | Detail |
|---|---|
| Chip | ESP32-C6FH8, RISC-V 160MHz, single-core |
| Flash | 8MB embedded |
| Display | 1.47" ST7789, 172×320, SPI2 DMA |
| Storage | SD card, SPI2 shared bus |
| Wireless | Wi-Fi 6, BT 5 LE, IEEE 802.15.4 (Thread / Zigbee / Matter) |
| LED | Single WS2812 RGB on GPIO 8 |
| Button | BOOT button GPIO 9 |

---

## Projects

| # | Project | Description |
|---|---|---|
| [01](projects/01_ntp_clock) | **NTP Clock** | Live NTP-synced clock with animated LVGL UI, 4 color themes, RGB LED ambient sync, button theme/LED control |
| [02](projects/02_wifi_monitor) | **Wi-Fi Monitor** | Passive 2.4 GHz promiscuous sniffer — channel utilization, RF quality index, HMAC-SHA256 device fingerprinting, LED health indicator |
| [11](projects/11_mcp_server_display) | **MCP Server Display** | ESP32-C6 as a Model Context Protocol (MCP) server — an AI assistant draws shapes and text on the LCD via JSON-RPC 2.0 over Wi-Fi; JPEG snapshots close the visual feedback loop |
| [12](projects/12_mcp_gpio) | **MCP GPIO** | MCP server for full GPIO control — digital I/O, ADC (mV), PWM, I2C scan, onboard RGB LED. Live LVGL dashboard on LCD shows every pin's mode and value, colour-coded. Portable `board_config.h` for other boards. |

---

## Feature Domains

| Area | Details |
|---|---|
| Embedded C / FreeRTOS | Queues, mutexes, DMA SPI, ISR-safe vs task callbacks, single-core task model |
| LVGL 8 | Animated arcs, canvas draw API, table dashboard, timer-dispatch pattern for thread safety |
| Networking | Wi-Fi 6 STA, HTTP server, JSON-RPC 2.0 (MCP), mDNS service discovery |
| AI / LLM integration | MCP server on bare metal, tool schema design, visual feedback loop, GPIO control via LLM |
| Hardware abstraction | Portable board_config.h, LEDC PWM channel management, ADC oneshot + calibration |
| Security | HMAC-SHA256 device fingerprinting, hourly salt rotation, no raw MAC storage, 4-layer GPIO safety |
| Testing | Python integration test suites across all MCP projects, snapshot verification |

---

## Repo Structure

```
esp32c6-lcd147-projects/
├── projects/             # One ESP-IDF project per subdirectory
├── shared/
│   └── components/       # Shared: LVGL 8.3.11, lcd_driver, led_strip
├── docs/
│   └── media/            # Board photos and demo videos
├── CLAUDE.md             # Agent context — board specs, APIs, build rules, LVGL gotchas
└── .claude/
    └── commands/         # Slash-command agent skills
        ├── build.md           # /build          — IDF activate + build with auto-recovery
        ├── flash.md           # /flash          — port detect + flash
        ├── new-project.md     # /new-project    — scaffold with correct patterns
        ├── hardware-specs.md  # /hardware-specs — C6 hardware reference (accelerators, RAM budget)
        ├── mcp-tool-design.md # /mcp-tool-design — MCP schema checklist (tool budget, error design)
        └── display-ui.md      # /display-ui      — 172×320 LCD layout rules, LVGL row math, colour guide
```

---

## Agent Skills (Claude Code)

Skills are slash commands invoked inside a Claude Code session — type `/build 12_mcp_gpio` instead of remembering the full IDF invocation. Each skill encodes hard-won project knowledge (target quirks, recovery steps, board-specific rules) so the agent applies it correctly without being told every session. New projects automatically inherit all accumulated lessons.

| Skill | Usage | What it knows |
|---|---|---|
| `/build` | `/build <name>` | Activates IDF, sets `esp32c6` target if missing, auto-recovers from IRAM overflow, bad `dependencies.lock`, partition-too-small |
| `/flash` | `/flash <name>` | Auto-detects serial port, handles mid-flash reconnect (normal for this board) |
| `/new-project` | `/new-project <name>` | Scaffolds with `CMakeLists.txt`, 2MB `partitions.csv`, credential-safe `sdkconfig.defaults`, LVGL threading rules |
| `/hardware-specs` | `/hardware-specs` | Full C6 hardware reference: AES/SHA/HMAC accelerators, no JPEG/FPU/SIMD, RAM budget template, Wi-Fi modem sleep, `esp_new_jpeg` guidance |
| `/mcp-tool-design` | `/mcp-tool-design` | MCP design checklist: 6-component tool description framework, tool budget (max 8), negative guidance patterns, error channels, image content type |
| `/display-ui` | `/display-ui` | 172×320 LCD UI reference: pixel budget, LVGL row height math, column widths, colour palette, thread safety rules, common pitfalls |

---

## Requirements & Building

- [ESP-IDF 5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/)
- Target: `esp32c6`

```zsh
# Activate IDF (required once per shell session)
. ~/esp/esp-idf/export.sh

# First time only — set target
idf.py -C projects/<name> set-target esp32c6

# Build and flash
idf.py -C projects/<name> build
idf.py -C projects/<name> -p /dev/cu.usbmodem1101 flash
```

Each project uses `../../shared/components` via `EXTRA_COMPONENT_DIRS` — shared components (LVGL, lcd_driver) are available without duplication.
