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
| — | *(coming soon)* | |

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
    └── commands/         # Project-scoped agent skills
        ├── build.md      # /build  — activate IDF and build a project
        ├── flash.md      # /flash  — detect port and flash to board
        └── new-project.md # /new-project — scaffold a new project correctly
```

---

## Agentic Development

This repo is structured for use with **Claude Code** as the primary development agent.

The `CLAUDE.md` file gives the agent persistent context about the board, build system, and conventions so you don't repeat yourself across sessions. The `.claude/commands/` skills handle the repetitive operations:

```
/build <project_name>       # Build with correct IDF target and error recovery
/flash <project_name>       # Auto-detect port and flash
/new-project <name>         # Scaffold a new project with correct structure
```

Each skill encodes lessons learned — common failure modes, board-specific quirks, correct init order — so the agent handles them automatically rather than rediscovering them each session.

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
