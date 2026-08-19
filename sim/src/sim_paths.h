/**
 * Where the simulator keeps the state the device keeps in flash.
 *
 * Defaults to ./sim_data, overridable with the SIM_DATA_DIR environment
 * variable so several simulator instances can run side by side.
 */
#pragma once

#include <string>

const std::string& sim_data_dir();
std::string sim_nvs_dir();
std::string sim_littlefs_dir();
