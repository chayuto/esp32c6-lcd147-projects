# 01 — NTP Clock

A live NTP-synced clock with animated LVGL UI, theme switching via button, and RGB LED ambient sync.

## Demo

*(photo/video coming soon)*

## Features

- Real-time clock synced via NTP (configurable timezone, default Thailand ICT UTC+7)
- Animated seconds arc sweeping around the clock face
- Colon pulse and digit fade animations on time change
- 4 color themes: Cyan / Amber / Green / Purple
- RGB LED glows in ambient theme color, pulses each second, flashes on minute change
- Boot screen with spinner showing WiFi → NTP connect progress
- Short press GPIO 9 (BOOT button) → cycle theme
- Long press GPIO 9 → toggle LED on/off

## Hardware

| Feature | Detail |
|---|---|
| Board | ESP32-C6FH8, 8MB flash |
| Display | 1.47" ST7789 172×320 via SPI |
| LED | Single WS2812 RGB on GPIO 8 |
| Button | BOOT button GPIO 9 |

## Configuration

Edit `sdkconfig.defaults` before building:

```
CONFIG_WIFI_SSID="your_ssid"
CONFIG_WIFI_PASSWORD="your_password"
CONFIG_TIMEZONE="ICT-7"   # POSIX tz string — ICT-7 = Thailand UTC+7
```

Other timezone examples:
- `UTC0` — UTC
- `EST5EDT,M3.2.0,M11.1.0` — US Eastern
- `CET-1CEST,M3.5.0,M10.5.0/3` — Central Europe
- `JST-9` — Japan

## Build & Flash

```zsh
. ~/esp/esp-idf/export.sh

# First time only
idf.py -C projects/01_ntp_clock set-target esp32c6

idf.py -C projects/01_ntp_clock build
idf.py -C projects/01_ntp_clock -p /dev/cu.usbmodem1101 flash
```

## Project Structure

```
01_ntp_clock/
├── main/
│   ├── main.c          # Init, state machine, task orchestration
│   ├── wifi_connect.c  # WiFi STA connect with timeout
│   ├── ntp_sync.c      # SNTP sync, timezone, get_local_time()
│   ├── theme.c         # 4 themes — screen colors + LED RGB values
│   ├── led_ctrl.c      # LED task with crossfade engine
│   ├── button.c        # GPIO 9 debounce, short/long press queue
│   ├── boot_screen.c   # Boot/connecting LVGL screen with spinner
│   ├── clock_face.c    # Clock UI — arc, labels, animations
│   └── app_state.h     # Shared state enum + mutex
├── partitions.csv      # 2MB app partition
└── sdkconfig.defaults  # WiFi credentials + font config
```
