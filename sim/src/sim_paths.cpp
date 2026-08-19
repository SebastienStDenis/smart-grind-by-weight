#include "sim_paths.h"

#include <cstdlib>
#include <filesystem>

const std::string& sim_data_dir() {
    static const std::string dir = [] {
        const char* override_dir = std::getenv("SIM_DATA_DIR");
        std::string path = (override_dir && *override_dir) ? override_dir : "sim_data";
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path;
    }();
    return dir;
}

std::string sim_nvs_dir() {
    return (std::filesystem::path(sim_data_dir()) / "nvs").string();
}

std::string sim_littlefs_dir() {
    return (std::filesystem::path(sim_data_dir()) / "littlefs").string();
}
