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
| `SIM_GATEWAY_URL` | Train gateway base URL, e.g. `http://192.168.1.134:8585`. Falls back to `GATEWAY_URL` in the repo's `.env` |
| `SIM_WIFI_SSID` | Cosmetic; only needs to be non-empty for the gateway config to count as provisioned. Falls back to `WIFI_SSID` in `.env` |
| `SIM_SNAPSHOT` | Write one BMP of the panel to this path, then carry on |
| `SIM_SNAPSHOT_MS` | How long to wait before the snapshot, default 3000 |
| `SIM_SNAPSHOT_SDL` | Capture the frame SDL actually presented instead of LVGL's own output. The only way to see compositing faults |
| `SIM_SNAPSHOT_OBJ` | `top` or `sys` to capture an overlay layer instead of the active screen |

`sim_data/` holds everything the device keeps in flash: `nvs/` for Preferences
namespaces and `littlefs/` for grind session logs. Delete it to factory-reset.

### Trains screensaver

```bash
SIM_GATEWAY_URL=http://192.168.1.134:8585 ./build/grinder-sim
```

`TrainDataClient` polls that gateway over libcurl and parses the response with
the same cJSON path as the firmware, so routes, colours, catch dots, paging and
the stale/expired states are all live.

If `SIM_GATEWAY_URL` is unset the simulator reads `GATEWAY_URL` from the repo's
`.env`, the same file `grinder.py wifi --set` provisions the device from. That
file is gitignored, so it does not exist inside a git worktree; pass the
variable explicitly there.

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

- **Pixels are byte-swapped on the way to SDL.** Homebrew ships `sdl2` as
  sdl2-compat on top of SDL3, and its RGB565 texture upload takes the bytes in
  the opposite order: greys render green and dark glyphs pink, while black and
  white survive because they are palindromic in RGB565. `sim_display.cpp` wraps
  the backend's flush callback and swaps each buffer first, which cancels it out
  and keeps the simulator at the panel's real 16bpp depth. Rendering at 32bpp
  avoids the swap too, but the backend then delivers only half the rows.
- **Timing is not microsecond-faithful.** macOS schedules the task threads;
  `vTaskDelayUntil` is a sleep, not a tick-driven wake. Fine for UI and state
  machine work, wrong for tuning the prediction algorithm.
- **Core pinning is ignored.** Both "cores" are just threads.
- **The panel's rounded corners are not reproduced**, and brightness is
  emulated by compositing a black scrim rather than driving a backlight. The
  scrim is gamma-corrected and capped, because perceived lightness follows
  roughly the 1/2.2 power of luminance and a linear scrim made the dimmed
  screensaver state far darker than the real panel looks.
- **Heap figures come from the host**, so System Info reports the Mac's memory
  rather than the ESP32's 512 kB of internal RAM. LVGL's pool is also raised
  from the device's 256 kB to 2 MB, because a full-screen snapshot needs about
  500 kB on its own, so LVGL allocation pressure is not represented either.
- **The mock-hardware background tint is suppressed.** `UIManager::create_ui()`
  paints the screen dark green (`THEME_COLOR_BACKGROUND_MOCK`) whenever the mock
  load cell is compiled in, so a bench device cannot be mistaken for real
  hardware. The simulator needs the mock cell but wants the panel's real
  colours, so `sim_main.cpp` restores the background. The related
  `DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR` flag, which flips the background
  to dark yellow while the motor runs, is left off for the same reason.
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
