# 02 — Wi-Fi Monitor

Passive Wi-Fi 6 spectrum analyzer for ESP32-C6. No network connection required — the device sniffs 2.4 GHz traffic in promiscuous mode.

## Displays

- **Channel Utilization** — fraction of time the channel is occupied by RF transmissions
- **RF Quality Index** — composite score (CU%, average SNR, retry rate, network density)
- **Devices Nearby** — estimated unique device count via probe-request fingerprinting
- **Packet rate & queue drop rate** — sniffer health metrics

## Controls

| Action | Effect |
|---|---|
| Short press BOOT button | Cycle scan channel: CH1 → CH6 → CH11 → CH1 |
| Long press BOOT button | Toggle LED on/off |

LED color reflects channel health: green < 40% CU, orange 40–69%, red ≥ 70%.

## Build

```zsh
. ~/esp/esp-idf/export.sh
idf.py -C projects/02_wifi_monitor set-target esp32c6   # first time only
idf.py -C projects/02_wifi_monitor build
idf.py -C projects/02_wifi_monitor -p /dev/cu.usbmodem1101 flash
idf.py -C projects/02_wifi_monitor -p /dev/cu.usbmodem1101 monitor  # run in a real TTY
```

> Flash and monitor must be run as **separate commands** in an interactive terminal. Running them combined (`flash monitor`) in a non-TTY shell (pipes, scripts) causes monitor to exit immediately, aborting the flash mid-write and leaving corrupt firmware.

No configuration required. This project does not join any Wi-Fi network.

## Privacy

Device counting uses HMAC-SHA256 with hourly salt rotation. No MAC addresses are stored or logged. All observations expire after 60 seconds. Device count is an upper-bound estimate (accuracy ~75–96% depending on device OS version).

## Project Structure

```
02_wifi_monitor/
├── main/
│   ├── main.c              # Init, task orchestration, app state machine
│   ├── wifi_sniffer.c/.h   # Promiscuous RX callback, packet queue
│   ├── sniffer_tasks.c/.h  # Consumer tasks — dispatch from packet queue
│   ├── airtime.c/.h        # Air-time estimation from frame durations
│   ├── channel_util.c/.h   # Channel utilization rolling average
│   ├── rf_metrics.c/.h     # RF quality index, SNR, retry rate
│   ├── device_counter.c/.h # HMAC-SHA256 fingerprinting, hourly salt rotation
│   ├── ui_display.c/.h     # LVGL screen layout, metric label updates
│   ├── led_ctrl.c/.h       # LED task — green/orange/red health indicator
│   ├── button.c/.h         # GPIO 9 debounce, short/long press
│   └── app_state.h         # Shared state struct + mutex
└── partitions.csv          # 2MB app partition
```

## Known Limitations

- **2.4 GHz only** — ESP32-C6 has no 5 GHz radio
- **One channel at a time** — channel hop shows 1/3 coverage per channel when auto-hop is enabled (off by default; use the button for manual control)
- **OFDMA MU-MIMO** — parallel resource unit transmissions may not be fully decoded; air time is estimated from trigger frames
- **Device count accuracy** — iOS 16+ and Android 12+ randomise sequence numbers on MAC rotation, which can cause overcounting
