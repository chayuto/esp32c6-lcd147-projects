# esp32c6-lcd147-projects

A collection of ESP-IDF projects for the **ESP32-C6 with 1.47" ST7789 LCD** board.

This repo is built with an **agentic-first development workflow** — each project is developed with [Claude Code](https://claude.ai/code) as the primary development agent, using project-scoped skills and context defined in `CLAUDE.md` and `.claude/commands/`.

---

## Board

| Feature | Detail |
|---|---|
| Chip | ESP32-C6FH8, RISC-V 160MHz |
| Flash | 8MB embedded |
| Display | 1.47" ST7789, 172×320, SPI |
| Storage | SD card, SPI (shared bus with LCD) |
| Wireless | Wi-Fi 6, BT 5 LE, IEEE 802.15.4 (Thread / Zigbee / Matter) |
| LED | RGB LED strip |

---

## Projects

| # | Project | Key Tech |
|---|---|---|
| [01](projects/01_ntp_clock) | NTP Clock | LVGL animated clock, NTP sync, 4 themes, RGB LED ambient, button control |
| [02](projects/02_wifi_monitor) | Wi-Fi Monitor | Passive 2.4 GHz sniffer, channel utilization, RF quality index, device count, LED health indicator |

---

## Repo Structure

```
esp32c6-lcd147-projects/
├── projects/             # One ESP-IDF project per subdirectory
├── shared/
│   └── components/       # Shared components across all projects (LVGL, wifi_connect, etc.)
├── docs/
│   └── media/            # Board photos and demo videos
├── CLAUDE.md             # Agent context — board specs, build rules, conventions
└── .claude/
    └── commands/              # Project-scoped agent skills
        ├── build.md           # /build          — activate IDF and build a project
        ├── flash.md           # /flash          — detect port and flash to board
        ├── new-project.md     # /new-project    — scaffold a new project correctly
        ├── hardware-specs.md  # /hardware-specs — ESP32-C6 hardware capabilities & limits
        └── mcp-tool-design.md # /mcp-tool-design — MCP server tool schema checklist
```

---

## Agent Skills (Claude Code)

This repo is built with **[Claude Code](https://claude.ai/code)** as the primary development agent. Slash-command skills handle repetitive operations and encode hardware knowledge learned across all projects:

| Skill | Usage | What it knows |
|---|---|---|
| `/build` | `/build <name>` | Activates IDF, sets `esp32c6` target if missing, auto-recovers from IRAM overflow / bad `dependencies.lock` / partition-too-small errors |
| `/flash` | `/flash <name>` | Auto-detects serial port, handles mid-flash reconnect (normal for this board) |
| `/new-project` | `/new-project <name>` | Scaffolds with correct `CMakeLists.txt`, 2MB `partitions.csv`, credential-safe `sdkconfig.defaults` pattern, LVGL threading rules pre-documented |
| `/hardware-specs` | `/hardware-specs` | **New** — Full ESP32-C6 hardware reference: what accelerators exist (AES, SHA, HMAC), what doesn't (no JPEG, no FPU, no SIMD), DMA peripheral list, RAM budget template, Wi-Fi modem sleep gotcha, `esp_new_jpeg` guidance |
| `/mcp-tool-design` | `/mcp-tool-design` | **New** — MCP server design checklist: 6-component tool description framework, tool budget rules (5–8 max), negative guidance patterns, error response design, image content type for display servers, `/ping` health endpoint |

Project context lives in `CLAUDE.md` — board pinout, shared component APIs, critical build rules, LVGL gotchas — so the agent has full context from the first message of every session without repeated explanation.

---

## Requirements

- [ESP-IDF 5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/)
- Target: `esp32c6`

## Building

```zsh
# Activate IDF (required once per shell session)
. ~/esp/esp-idf/export.sh

# First time only — set target
idf.py -C projects/<name> set-target esp32c6

# Build
idf.py -C projects/<name> build

# Flash (replace port as needed)
idf.py -C projects/<name> -p /dev/cu.usbmodem1101 flash
```

> Each project in `projects/` uses `../../shared/components` via `EXTRA_COMPONENT_DIRS` in its `CMakeLists.txt`, so shared components (LVGL etc.) are available without duplication.
