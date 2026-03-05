# Build current or specified project

Activate ESP-IDF and build. Usage: /build [project_name]

If a project name is given, build `projects/<project_name>`.
If no project name is given, ask which project to build or detect from context.

Steps:
1. Run `. ~/esp/esp-idf/export.sh 2>/dev/null` to activate IDF
2. Run `idf.py -C projects/<name> build 2>&1 | tail -25`
3. If build fails with "IDF_TARGET not set" or IRAM overflow → the target is wrong. Run `idf.py -C projects/<name> set-target esp32c6` then rebuild.
4. If build fails with path errors in dependencies.lock → delete `projects/<name>/dependencies.lock` and rebuild.
5. Report success with binary size, or show the specific error lines.
