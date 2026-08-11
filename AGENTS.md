# Repository Guidelines

## Project Structure & Module Organization

This is an ESP32-S3 firmware project built with ESP-IDF. The application entry point is in `main/` (`main.c` and `task.c`). Device features are split into components:

- `components/all_control/`: task orchestration, Wi-Fi, HTTP routes, embedded web pages, keys, and WS2812 control.
- `components/myusb/`: CherryUSB descriptors, CDC/UART forwarding, MSC/FAT handling, and CMSIS-DAP glue.
- `components/DAP/`: CMSIS-DAP implementation and ARM core headers.
- `components/cherryrb/`: ring buffer used by serial forwarding.
- `components/cherry-embedded__cherryusb/`: vendored USB stack; avoid unrelated edits.

Project configuration is in `CMakeLists.txt`, `sdkconfig*`, `partitions.csv`, and `dependencies.lock`. `DAPLink-main/` is reference/vendor material and is not part of the active build unless explicitly wired in.

## Build, Test, and Development Commands

Load the ESP-IDF environment before running commands (the project targets ESP-IDF 5.5):

```bash
source ~/esp/v5.5/esp-idf/export.sh
idf.py build                         # compile firmware
idf.py -p <PORT> flash monitor       # flash a board and view logs
idf.py -p <PORT> monitor             # view logs only
idf.py menuconfig                    # change generated SDK configuration
idf.py size size-components size-files # inspect image size
```

## Coding Style & Naming Conventions

Use C with four-space indentation, braces on the same line, and small component-local helpers. Keep functions and variables in `snake_case`; use existing ESP-IDF types and `ESP_LOG*` logging. Keep public headers under each component's `include/` directory. Web assets embedded by `components/all_control/CMakeLists.txt` must be rebuilt after changes.

## Testing Guidelines

There is no project-wide unit-test target. Treat `idf.py build` as the minimum validation. Hardware changes should be tested with `idf.py -p <PORT> flash monitor`, checking USB CDC/DAP, Wi-Fi routes, and serial logging. The optional `pytest_usb_device_msc.py` uses `pytest` and `pytest-embedded`; run it with the ESP-IDF pytest setup when a compatible board is connected.

## Commit & Pull Request Guidelines

History uses short imperative summaries such as `feat: update wifi/web ui controls...`, `docs: rewrite README...`, and `release1.0.1`. Follow that style, preferably with a scope (`feat:`, `fix:`, `docs:`), and keep each commit focused. Pull requests should explain behavior and hardware impact, list validation commands and board/port details, link related issues, and include screenshots or serial logs for web UI or device-facing changes. Call out any `sdkconfig`, partition, pin, or credential changes explicitly.

## Security & Configuration Tips

Do not commit real Wi-Fi credentials, private keys, or generated local SDK/IDE paths. Review `sdkconfig` and `partitions.csv` before changing flash settings; the checked-in configuration and defaults differ. Preserve `dependencies.lock` unless dependency resolution is intentional.
