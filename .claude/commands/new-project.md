# Scaffold a new project

Create a new ESP-IDF project under `projects/`. Usage: /new-project <name>

Steps:
1. Create `projects/<name>/` directory structure:
   - `projects/<name>/main/`
   - `projects/<name>/main/CMakeLists.txt`
   - `projects/<name>/main/main.c`
   - `projects/<name>/CMakeLists.txt`
   - `projects/<name>/sdkconfig.defaults`
   - `projects/<name>/README.md`

2. `projects/<name>/CMakeLists.txt` must include:
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

4. `projects/<name>/sdkconfig.defaults`:
```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
```

5. After scaffolding, run:
   `. ~/esp/esp-idf/export.sh 2>/dev/null && idf.py -C projects/<name> set-target esp32c6`

6. Update `projects/<name>/README.md` with project description and build instructions.
7. Add the project to the root `README.md` project table.
