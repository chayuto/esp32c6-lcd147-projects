# CLAUDE.md — esp32c6-lcd147-projects

## Project Overview

Multi-project portfolio repo for ESP32-C6 with 1.47" ST7789 LCD.
Each project lives in `projects/`. Shared ESP-IDF components live in `shared/components/`.

## Repo Structure

```
esp32c6-lcd147-projects/
├── shared/components/    # Shared: lvgl, led_strip, wifi_connect, sd_card
├── projects/             # One subdirectory per project
├── ref/                  # Vendor reference/test code — gitignored, do not modify
├── docs/
│   ├── internal/         # Private dev notes — gitignored
│   └── media/            # Board photos/videos for READMEs
└── CLAUDE.md
```

## Board

- **Chip:** ESP32-C6FH8, RISC-V 160MHz, 8MB embedded flash
- **IDF Target:** `esp32c6` — ALWAYS set this, default is esp32 (Xtensa) which will fail
- **Display:** 1.47" ST7789, 172×320, SPI2 (SCLK=7, MOSI=6, CS=14, DC=15, RST=21)
- **SD Card:** SPI2 shared bus (MISO=5, CS=4) — same bus as LCD, different CS
- **RGB LED:** via espressif/led_strip component
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

# New project — set target first (one time)
idf.py -C projects/<name> set-target esp32c6
```

## Each Project's CMakeLists.txt Must Include

```cmake
set(EXTRA_COMPONENT_DIRS "../../shared/components")
```

This points to shared components so lvgl, wifi_connect, sd_card etc. are available without duplication.

## Critical Rules

- **Never build without setting target to esp32c6** — default esp32 builds will link fail with IRAM overflow
- **Delete `dependencies.lock` if moving/copying a project** — it contains absolute paths that break the build
- **SD and LCD share SPI2 bus** — this is fine, ESP-IDF handles arbitration via CS pins
- **`ref/` is gitignored** — do not commit or modify it
- **`docs/internal/` is gitignored** — private notes only

## Shared Components (planned)

| Component | Status | Used by |
|---|---|---|
| `lvgl__lvgl` | ready | all UI projects |
| `espressif__led_strip` | ready | board_test |
| `wifi_connect` | planned | weather, file server, OTA |
| `sd_card` | planned | file server, data logger, image viewer |
