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
#include <lvgl.h>
#include <freertos/FreeRTOS.h>

#include <cstdlib>
#include <fstream>
#include <string>
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

/** UIManager::create_ui() tints the screen dark green whenever the mock load
 *  cell is compiled in, so a bench device cannot be mistaken for real hardware.
 *  The simulator needs the mock cell but wants the panel's real colours, so the
 *  screen's own background is restored. A local style outranks the added one. */
void restore_panel_background() {
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(THEME_COLOR_BACKGROUND), 0);
}

/** Reads one key out of the repo's .env, the same file `grinder.py wifi --set`
 *  provisions the device from. */
std::string value_from_dotenv(const char* key) {
    for (const char* candidate : {".env", "../.env"}) {
        std::ifstream file(candidate);
        if (!file) continue;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            size_t separator = line.find('=');
            if (separator == std::string::npos) continue;
            if (line.compare(0, separator, key) != 0) continue;

            std::string value = line.substr(separator + 1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return std::string();
}

/** Seeds the gateway config so the Trains screensaver can reach a real gateway
 *  without going through BLE provisioning. SIM_GATEWAY_URL wins; otherwise the
 *  repo's .env is used, so the simulator points where the device points. */
void seed_network_config() {
    const char* env_url = std::getenv("SIM_GATEWAY_URL");
    std::string url = (env_url && *env_url) ? env_url : value_from_dotenv("GATEWAY_URL");
    if (url.empty()) {
        Serial.printf("[SIM] No train gateway configured - set SIM_GATEWAY_URL or GATEWAY_URL in .env\n");
        return;
    }

    const char* env_ssid = std::getenv("SIM_WIFI_SSID");
    std::string ssid = (env_ssid && *env_ssid) ? env_ssid : value_from_dotenv("WIFI_SSID");
    if (ssid.empty()) ssid = "sim-host";

    Preferences prefs;
    prefs.begin(NET_PREFS_NAMESPACE, false);
    prefs.putString(NET_PREFS_KEY_GATEWAY_URL, url.c_str());
    prefs.putString(NET_PREFS_KEY_SSID, ssid.c_str());
    prefs.end();

    Serial.printf("[SIM] Train gateway: %s\n", url.c_str());
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
    restore_panel_background();
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
