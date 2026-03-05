# Scaffold a new project

Create a new ESP-IDF project under `projects/`. Usage: /new-project <name>

Steps:

1. Create directory structure:
   - `projects/<name>/main/`
   - `projects/<name>/main/CMakeLists.txt`
   - `projects/<name>/main/main.c`
   - `projects/<name>/main/Kconfig.projbuild`  ← for any user-configurable settings
   - `projects/<name>/CMakeLists.txt`
   - `projects/<name>/sdkconfig.defaults`       ← gitignored, user fills credentials locally
   - `projects/<name>/sdkconfig.defaults.template` ← committed, placeholder values only
   - `projects/<name>/partitions.csv`           ← copy from ref/partitions.csv (2MB app)
   - `projects/<name>/README.md`

2. `projects/<name>/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../../shared/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<name>)
```

3. `projects/<name>/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS ".")
```

4. `projects/<name>/sdkconfig.defaults` (local only, gitignored):
```
CONFIG_IDF_TARGET="esp32c6"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_BT_ENABLED=n
```
   Add WiFi credentials here if the project needs WiFi.

5. `projects/<name>/sdkconfig.defaults.template` (committed to git):
   Same as sdkconfig.defaults but with placeholder values for any secrets.

6. Copy `ref/partitions.csv` to `projects/<name>/partitions.csv` — default IDF partition is 1MB
   which is too small for LVGL projects. This gives 2MB app partition.

7. After scaffolding, set target:
   `. ~/esp/esp-idf/export.sh 2>/dev/null && idf.py -C projects/<name> set-target esp32c6`

8. Update `projects/<name>/README.md` with description, build instructions, config options.
9. Add the project to the root `README.md` project table.

## Known LVGL Gotchas

- Use `lv_color_make(r, g, b)` — NOT `LV_COLOR_MAKE(r, g, b)` in function arguments.
  The macro form is a compound literal that causes compile errors in some contexts.
- All LVGL object mutations must happen inside `lv_timer` callbacks.
  Never call `lv_label_set_text()` or similar directly from a FreeRTOS task.
- `lv_spinner_create(parent, speed_ms, arc_degrees)` takes 3 arguments total.
- Arc angle 270° = top of circle (12 o'clock). Angles increase clockwise.

## Shared Components Available

| Component | Include in main/CMakeLists.txt REQUIRES |
|---|---|
| `lcd_driver` | `lcd_driver` — exposes LCD_Init(), BK_Light(), LVGL_Init() |
| `lvgl__lvgl` | pulled in automatically via lcd_driver |
| `espressif__led_strip` | `espressif__led_strip` |

## Credential Pattern

For any project with WiFi:
- `sdkconfig.defaults` — real credentials, gitignored
- `sdkconfig.defaults.template` — placeholder, committed
- README instructs users to copy template → sdkconfig.defaults and fill in values
