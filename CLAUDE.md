# CLAUDE.md

This file gives Claude Code the repo-specific context needed to work effectively in this ESP-IDF project.

## Project Overview

`key_wifi` is ESP32-S3 firmware built with ESP-IDF. It combines CherryUSB device functions, CMSIS-DAP over WinUSB, USB CDC-to-UART forwarding, MSC/FAT presentation, SoftAP/STA Wi-Fi control, an embedded HTTP UI, keys, and WS2812 LED control.

The runtime entry is `app_main()` in `main/main.c`. It initializes NVS, then calls `all_control_main()` in `components/all_control/all_control.c`. That function creates the shared LED queue and starts the core FreeRTOS tasks:

- `ws2812_task`
- `usb_task`
- `dap_task`
- `key_task`
- `wifi_task`

## Common Commands

Load ESP-IDF before building. The README examples use ESP-IDF v5.1.5:

```bash
source ~/esp/v5.1.5/esp-idf/export.sh
```

Build:

```bash
idf.py build
```

Configure:

```bash
idf.py menuconfig
```

Flash and monitor:

```bash
idf.py -p <PORT> flash monitor
```

Monitor only:

```bash
idf.py -p <PORT> monitor
```

Inspect size:

```bash
idf.py size
idf.py size-components
idf.py size-files
```

No automated test target is currently documented. Use `idf.py build` for compile validation, then hardware validation with `idf.py -p <PORT> flash monitor` when a board is available.

## Build And Configuration Notes

- Top-level project name is `key_wifi` in `CMakeLists.txt`.
- `main/idf_component.yml` constrains ESP-IDF to `^5.0` and declares `espressif/button`.
- Checked-in `sdkconfig` targets ESP32-S3 with 16 MB flash and custom `partitions.csv`.
- `sdkconfig.defaults` still contains a 4 MB flash default. Confirm the intended flash size before regenerating SDK config or changing partitions.
- `partitions.csv` has 2 MB `factory`, `ota_0`, and `ota_1` app partitions plus a 9 MB FAT `storage` partition.
- Web UI files are embedded via `EMBED_FILES` in `components/all_control/CMakeLists.txt`; rebuild firmware after changing `root.html` or `serial_view.html`.

## Architecture Map

### Main Control

- `main/main.c` owns NVS init and delegates to `all_control_main()`.
- `components/all_control/all_control.c` is the task orchestrator and owns the global `xQueueLed` handle.
- `components/all_control/include/all_control.h` exposes the task entrypoints, `tws2812Def`, and AP/STA helper APIs.

### Wi-Fi, Web UI, Keys, And LEDs

Most device control code lives in `components/all_control/`:

- `src/mywifi.c` initializes AP/STA networking. Default AP credentials are SSID `dtest` and password `12345678`.
- `src/webserver.c` starts the ESP HTTP server and registers routes.
- `src/ws2812.c` consumes `tws2812Def` messages from `xQueueLed`.
- `src/key.c` handles local key input.

Registered HTTP routes in `src/webserver.c`:

- `GET /` serves embedded `root.html` and substitutes `%IP%` with the AP IP.
- `GET /control?state=on|off` is a stub-style LED control endpoint.
- `POST /rgbcontrol` accepts JSON `r`, `g`, and `b`, clamps each channel to 0-255, then queues a WS2812 update.
- `GET /serial_view` serves embedded `serial_view.html`.
- `GET /serial` returns the in-memory CDC/UART serial log.
- `GET /wifi_status` returns AP/STA enabled state as JSON.
- `GET /wifi_control?ap=1&sta=0` switches AP/STA mode; at least one mode must remain enabled.

### USB, DAP, CDC/UART, And MSC

USB integration is in `components/myusb/`:

- `myusb.c` registers CherryUSB descriptors/endpoints for WinUSB/DAP, CDC ACM, and MSC; initializes UART2; forwards CDC OUT to UART and UART RX to CDC IN.
- `myDAP.c` connects WinUSB packets to the CMSIS-DAP command processor and runs `chry_dap_handle()` from `dap_task`.
- `usbAT.c` implements a small JSON-over-CDC command path. It can save AP credentials to NVS and apply them to Wi-Fi AP config.
- `virtualfat.c` implements MSC/FAT sector handling and virtual FAT content.
- `DAP_handle.c` exists as imported/reference code but is currently commented out in `components/myusb/CMakeLists.txt`.

Important USB constants are in `components/myusb/include/myusb.h`: UART pins, endpoint numbers, VID/PID, interface count, MSC enablement, and `current_dap_mode`.

CDC serial traffic is mirrored into a 4096-byte in-memory log in `myusb.c`. The webserver reads it through `usb_serial_log_snapshot()` for `/serial`.

### Imported Code

- `components/cherry-embedded__cherryusb/` is the CherryUSB stack and platform support. Avoid broad edits there unless working directly on USB stack integration.
- `components/cherryrb/` provides the ring buffer used by the CDC/UART path.
- `components/DAP/` contains the CMSIS-DAP implementation and configuration.
- `DAPLink-main/` is a vendored DAPLink source/reference tree and is not part of the active ESP-IDF component build unless explicitly wired in.

## Working Notes

- Match the existing C style: direct ESP-IDF calls, small component-local helpers, and sparse comments.
- Be careful with global state shared across FreeRTOS tasks, especially `xQueueLed`, USB endpoint flags, ring buffers, and Wi-Fi mode state.
- Prefer focused changes in active project components over refactoring vendored/imported code.
- The repository may have local SDK config and generated-file changes. Do not normalize `sdkconfig`, `sdkconfig.old`, `dependencies.lock`, or IDE files unless the task specifically requires it.
