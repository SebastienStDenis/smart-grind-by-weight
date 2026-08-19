# macOS Simulator

Runs the grinder firmware on a Mac, drawing the real LVGL interface into an SDL
window at the panel's native 280x456.

This is not a mock-up of the UI. The simulator compiles the firmware's own
`src/ui`, `src/controllers`, `src/system`, `src/logging`, `src/tasks` and
`src/network` and boots them through the real `setup()`/`loop()` in
`src/main.cpp`, against the same LVGL version the device links. Only the layers
that touch silicon are substituted.

## Setup

```bash
brew install sdl2 cjson
```

LVGL is taken from the PlatformIO checkout (`.pio/libdeps/*/lvgl`). If you have
never built the firmware, run `pio pkg install` first, or point the build at
your own copy with `LVGL_DIR=/path/to/lvgl`.

## Running

```bash
cd sim
make -j8
./build/grinder-sim
```

or `make run`.

- **Click** to tap, **drag** to swipe (mode carousel, screensaver pages).
- **`-` / `+`** scale the window, **`0`** returns it to 1:1. The panel is a
  fixed 280x456, so the window scales rather than reflows; `SIM_ZOOM=2` sets the
  starting scale.
- Closing the window quits.

### Environment

| Variable | Effect |
| --- | --- |
| `SIM_ZOOM` | Initial window scale, 0.5-4.0 (default 1.0) |
| `SIM_DATA_DIR` | Where NVS and LittleFS live (default `./sim_data`) |
| `SIM_GATEWAY_URL` | Train gateway base URL, e.g. `http://192.168.1.134:8585` |
| `SIM_WIFI_SSID` | Cosmetic; only needs to be non-empty for the gateway config to count as provisioned |

`sim_data/` holds everything the device keeps in flash: `nvs/` for Preferences
namespaces and `littlefs/` for grind session logs. Delete it to factory-reset.

### Trains screensaver

```bash
SIM_GATEWAY_URL=http://192.168.1.134:8585 ./build/grinder-sim
```

`TrainDataClient` polls that gateway over libcurl and parses the response with
the same cJSON path as the firmware, so routes, colours, catch dots, paging and
the stale/expired states are all live.

## What is substituted

| Device | Simulator |
| --- | --- |
| CO5300 AMOLED over QSPI | LVGL's SDL backend (`sim/src/sim_display.cpp`) |
| FT3168 touch over I2C | Mouse position and button, fed through the real `TouchDriver` |
| HX711 load cell | `MockHX711Driver`, the firmware's own mock: flow rate, motor latency, pulse dosing and noise |
| Motor over RMT | A flag that drives the mock cell |
| FreeRTOS | `std::thread`, mutexes and blocking deques (`sim/src/sim_freertos.cpp`) |
| NVS Preferences | One file per namespace under `sim_data/nvs` |
| LittleFS | A directory under `sim_data/littlefs` |
| WiFi | The Mac's existing connection; association always succeeds |
| HTTPClient | libcurl |
| NimBLE, OTA, data export | Stubbed - the adapter reports disabled |

Because the mock load cell and the real `GrindController` are both compiled in,
a weight-mode grind runs its actual nine-phase state machine: purge, tare,
predictive phase, pulse corrections and settling all execute against simulated
flow.

## Known differences

- **Timing is not microsecond-faithful.** macOS schedules the task threads;
  `vTaskDelayUntil` is a sleep, not a tick-driven wake. Fine for UI and state
  machine work, wrong for tuning the prediction algorithm.
- **Core pinning is ignored.** Both "cores" are just threads.
- **The panel's rounded corners and brightness curve are not reproduced.**
  Brightness is emulated as a black scrim, which is what the screensaver dim
  step looks like, but it is not the AMOLED's response.
- **Heap figures come from the host**, so System Info reports the Mac's memory
  rather than the ESP32's 512 kB of internal RAM. LVGL's own 256 kB pool is
  kept at its device size, so LVGL allocation pressure is still represented.
- **The scale starts calibrated and zeroed.** The mock cell has a fixed
  calibration factor, and `sim_main.cpp` zeroes it against the mock's known
  empty-pan baseline; otherwise every launch would open on the calibration
  screen reading about -1041 g.
- **App Nap can throttle the process** when the window is not focused, which
  shows up as long task cycle times in the heartbeat log.

## Layout

```
sim/
  Makefile          build rules; firmware source list lives here
  lv_conf.h         includes the firmware's lv_conf.h, overrides SDL + allocator
  include/          headers standing in for Arduino, ESP-IDF, FreeRTOS, NimBLE
  src/              the simulator's own implementations
```

`sim/include` is only on the simulator's include path, so nothing here affects
the firmware build.
