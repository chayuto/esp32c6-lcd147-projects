# CLAUDE.md — esp32c6-lcd147-projects

## Project Overview

Multi-project portfolio repo for ESP32-C6 with 1.47" ST7789 LCD.
Each project lives in `projects/`. Shared ESP-IDF components live in `shared/components/`.

## Repo Structure

```
esp32c6-lcd147-projects/
├── shared/components/    # Shared: lvgl__lvgl, lcd_driver, espressif__led_strip
├── projects/             # One subdirectory per project
├── ref/                  # Vendor reference/test code — gitignored, do not modify
├── docs/
│   ├── internal/         # Private dev notes — gitignored
│   └── media/            # Board photos/videos for READMEs
├── CLAUDE.md
└── .claude/commands/     # Agent skills: /build /flash /new-project
```

## Board

- **Chip:** ESP32-C6FH8, RISC-V 160MHz, 8MB embedded flash
- **IDF Target:** `esp32c6` — ALWAYS set this, default is esp32 (Xtensa) which will fail
- **Display:** 1.47" ST7789, 172×320 portrait, SPI2 (SCLK=7, MOSI=6, CS=14, DC=15, RST=21)
- **SD Card:** SPI2 shared bus (MISO=5, CS=4) — same bus as LCD, different CS, works fine
- **RGB LED:** Single WS2812 on GPIO 8 via espressif/led_strip + RMT
- **Button:** GPIO 9 (BOOT button) — pull-up, LOW when pressed, safe to use after boot
- **Serial port (macOS):** `/dev/cu.usbmodem1101`

## ESP-IDF Environment

- **Version:** 5.5
- **Install:** `~/esp/esp-idf`
- **Activate:** `. ~/esp/esp-idf/export.sh`
- **Python:** 3.14.3 managed virtualenv at `~/.espressif/python_env/idf5.5_py3.14_env`

## Build & Flash Commands

```zsh
# Activate IDF (required in every new shell)
. ~/esp/esp-idf/export.sh

# Build a project
idf.py -C projects/<name> build

# Flash
idf.py -C projects/<name> -p /dev/cu.usbmodem1101 flash

# New project — set target first (one time only, wipes build dir)
idf.py -C projects/<name> set-target esp32c6
```

## Each Project's CMakeLists.txt Must Include

```cmake
set(EXTRA_COMPONENT_DIRS "../../shared/components")
```

## Shared Components

| Component | Path | Exposes |
|---|---|---|
| `lcd_driver` | `shared/components/lcd_driver` | `LCD_Init()`, `BK_Light()`, `LVGL_Init()`, all pin defs |
| `lvgl__lvgl` | `shared/components/lvgl__lvgl` | LVGL 8.3.11 — pulled in via lcd_driver |
| `espressif__led_strip` | `shared/components/espressif__led_strip` | `led_strip_new_rmt_device()`, `led_strip_set_pixel()` |

## Partition Table

Default IDF partition = 1MB app. Too small for LVGL projects. Always use custom partition:
- Copy `ref/partitions.csv` to the project (gives 2MB app partition)
- Add to `sdkconfig.defaults`:
  ```
  CONFIG_PARTITION_TABLE_CUSTOM=y
  CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
  ```

## Credential Management (Public Repo)

`sdkconfig.defaults` is gitignored — never committed. Pattern for every project:
- `sdkconfig.defaults` — real credentials, local only
- `sdkconfig.defaults.template` — placeholder values, committed to git
- README instructs users: copy template → sdkconfig.defaults, fill in WiFi credentials

## Critical Rules

- **Never build without setting target to esp32c6** — default esp32 builds fail with IRAM overflow
- **Delete `sdkconfig` when changing `sdkconfig.defaults`** — stale sdkconfig ignores new defaults
- **Delete `dependencies.lock` if moving/copying a project** — contains absolute paths
- **`ref/` is gitignored** — do not commit or modify
- **`docs/internal/` is gitignored** — private notes only
- **`sdkconfig.defaults` is gitignored** — credentials must never be committed

## LCD / SPI Gotchas

- `LCD_Init()` calls `spi_bus_initialize(SPI2_HOST, ...)` internally — do NOT call it again elsewhere.
  The vendor ref project omitted this (SPI bus was a side effect of `SD_SPI.c`); our shared component
  fixed this by including it directly in `LCD_Init()`.
- MISO is defined as -1 in `ST7789.h` — LCD doesn't need it. SD card projects must re-initialize SPI2
  with MISO=5 separately (note: `spi_bus_initialize` will return `ESP_ERR_INVALID_STATE` if called twice
  on the same host — check for that error and skip it as non-fatal).

## LVGL Gotchas (Learned in Production)

- Use `lv_color_make(r, g, b)` — NOT `LV_COLOR_MAKE(r, g, b)` in function call arguments.
  The uppercase macro is a compound literal that causes "expected expression" errors in C.
- All LVGL mutations must happen inside `lv_timer` callbacks, never directly from FreeRTOS tasks.
  WiFi/NTP tasks set shared state; LVGL timer polls state and updates UI.
  Violation = blank screen (LVGL crashes before first render). Even a single `lv_label_set_text()`
  from a FreeRTOS task can corrupt display state before `lv_timer_handler()` ever runs.
- `lv_spinner_create(parent, speed_ms, arc_degrees)` — 3 args.
- Arc 270° = 12 o'clock. Angles increase clockwise.
- Disable BT if not needed: `CONFIG_BT_ENABLED=n` saves significant flash and RAM.

## Agent Skills

Use these slash commands for common operations:

| Command | Description |
|---|---|
| `/build <name>` | Activate IDF and build, with automatic failure recovery |
| `/flash <name>` | Auto-detect port and flash to connected board |
| `/new-project <name>` | Scaffold correct project structure with all known patterns |
