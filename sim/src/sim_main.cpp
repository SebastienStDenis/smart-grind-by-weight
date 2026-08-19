/**
 * Simulator entry point.
 *
 * Runs the firmware's own setup()/loop() from src/main.cpp. The only
 * rearrangement is which thread runs what: SDL owns the main thread on macOS,
 * so the UI render task is executed there and the housekeeping loop() gets a
 * thread of its own. Every other task is created by TaskManager as usual.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>

#include <cstdlib>
#include <thread>

#include "config/constants.h"
#include "hardware/hardware_manager.h"
#include "sim_paths.h"

extern void setup();
extern void loop();
extern HardwareManager hardware_manager;

namespace {

constexpr const char* kUITaskName = "UIRender";

/** The mock load cell reports a fixed calibration factor, so the simulated
 *  scale is calibrated by construction; without the flag the UI would open on
 *  the calibration screen every launch. */
void seed_calibration_flag() {
    Preferences prefs;
    prefs.begin("load_cell", false);
    if (!prefs.isKey("calibrated")) {
        prefs.putBool("calibrated", true);
    }
    prefs.end();
}

/** The tare offset is runtime state on the device, zeroed by every boot. The
 *  mock cell's empty-pan reading is a known constant, so zeroing against it
 *  puts the simulated scale where a physical one sits after its first tare. */
void zero_simulated_scale() {
    WeightSensor* sensor = hardware_manager.get_weight_sensor();
    if (!sensor) return;

    while (!sensor->is_initialized()) {
        delay(50);
    }

    sensor->set_zero_offset(DEBUG_MOCK_BASELINE_RAW);
    Serial.printf("[SIM] Simulated scale zeroed at raw %d\n", (int)DEBUG_MOCK_BASELINE_RAW);
}

/** Seeds the gateway config from the environment so the Trains screensaver can
 *  reach a real gateway without going through BLE provisioning. */
void seed_network_config() {
    const char* url = std::getenv("SIM_GATEWAY_URL");
    if (!url || !*url) return;

    Preferences prefs;
    prefs.begin(NET_PREFS_NAMESPACE, false);
    prefs.putString(NET_PREFS_KEY_GATEWAY_URL, url);

    const char* ssid = std::getenv("SIM_WIFI_SSID");
    prefs.putString(NET_PREFS_KEY_SSID, (ssid && *ssid) ? ssid : "sim-host");
    prefs.end();

    Serial.printf("[SIM] Train gateway: %s\n", url);
}

void print_banner() {
    Serial.printf("\n");
    Serial.printf("  Smart Grind by Weight - macOS simulator\n");
    Serial.printf("  Data directory: %s\n", sim_data_dir().c_str());
    Serial.printf("  Drag to swipe, click to tap. Close the window to quit.\n");
    Serial.printf("  Panel is a fixed 280x456: press - / + to scale it, 0 for 1:1.\n");
    Serial.printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    /* Line buffering keeps the firmware's log readable when stdout is a file. */
    setvbuf(stdout, nullptr, _IOLBF, 0);

    print_banner();
    seed_calibration_flag();
    seed_network_config();

    sim_set_main_thread_task(kUITaskName);

    setup();
    zero_simulated_scale();

    std::thread housekeeping([] {
        while (true) {
            loop();
        }
    });
    housekeeping.detach();

    sim_run_main_thread_task(kUITaskName);
    return 0;
}
