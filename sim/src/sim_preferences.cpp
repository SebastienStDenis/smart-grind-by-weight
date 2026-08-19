/**
 * Preferences (NVS) backed by one file per namespace under the simulator data
 * directory, so settings survive a restart the way flash does on the device.
 */

#include <Preferences.h>

#include <esp_err.h>
#include <nvs_flash.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "sim_paths.h"

namespace fs = std::filesystem;

namespace {

std::mutex& store_mutex() {
    static std::mutex mutex;
    return mutex;
}

fs::path namespace_path(const std::string& name) {
    return fs::path(sim_nvs_dir()) / (name + ".nvs");
}

/* Values are length-prefixed so credentials and labels round-trip byte for
 * byte, whatever they contain. */
void write_store(const fs::path& path, const std::map<std::string, std::string>& values) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;

    for (const auto& entry : values) {
        out << entry.first << '\n' << entry.second.size() << '\n' << entry.second << '\n';
    }
}

std::map<std::string, std::string> read_store(const fs::path& path) {
    std::map<std::string, std::string> values;
    std::ifstream in(path, std::ios::binary);
    if (!in) return values;

    std::string key;
    while (std::getline(in, key)) {
        std::string length_line;
        if (!std::getline(in, length_line)) break;

        size_t length = 0;
        try {
            length = (size_t)std::stoul(length_line);
        } catch (...) {
            break;
        }

        std::string value(length, '\0');
        if (length > 0) in.read(value.data(), (std::streamsize)length);
        in.get();  // trailing newline

        values[key] = value;
    }

    return values;
}

}  // namespace

bool Preferences::begin(const char* name, bool read_only) {
    std::lock_guard<std::mutex> lock(store_mutex());

    namespace_ = name ? name : "default";
    read_only_ = read_only;
    values_ = read_store(namespace_path(namespace_));
    open_ = true;
    return true;
}

void Preferences::end() {
    open_ = false;
    values_.clear();
}

void Preferences::load() {
    values_ = read_store(namespace_path(namespace_));
}

void Preferences::store() {
    if (read_only_) return;
    write_store(namespace_path(namespace_), values_);
}

size_t Preferences::putInt(const char* key, int32_t value) {
    return putString(key, std::to_string(value).c_str());
}

size_t Preferences::putUInt(const char* key, uint32_t value) {
    return putString(key, std::to_string(value).c_str());
}

size_t Preferences::putULong(const char* key, uint32_t value) {
    return putString(key, std::to_string(value).c_str());
}

size_t Preferences::putULong64(const char* key, uint64_t value) {
    return putString(key, std::to_string(value).c_str());
}

size_t Preferences::putFloat(const char* key, float value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.9g", (double)value);
    return putString(key, buffer);
}

size_t Preferences::putBool(const char* key, bool value) {
    return putString(key, value ? "1" : "0");
}

size_t Preferences::putString(const char* key, const String& value) {
    return putString(key, value.c_str());
}

size_t Preferences::putString(const char* key, const char* value) {
    if (!open_ || read_only_ || !key) return 0;

    std::lock_guard<std::mutex> lock(store_mutex());
    std::string text = value ? value : "";
    values_[key] = text;
    store();
    return text.size();
}

size_t Preferences::putBytes(const char* key, const void* value, size_t length) {
    if (!open_ || read_only_ || !key || !value) return 0;

    std::lock_guard<std::mutex> lock(store_mutex());
    values_[key] = std::string(static_cast<const char*>(value), length);
    store();
    return length;
}

size_t Preferences::getBytes(const char* key, void* buffer, size_t max_length) {
    if (!open_ || !key || !buffer) return 0;

    std::lock_guard<std::mutex> lock(store_mutex());
    auto it = values_.find(key);
    if (it == values_.end()) return 0;

    size_t length = it->second.size() < max_length ? it->second.size() : max_length;
    memcpy(buffer, it->second.data(), length);
    return length;
}

size_t Preferences::getBytesLength(const char* key) {
    if (!open_ || !key) return 0;

    std::lock_guard<std::mutex> lock(store_mutex());
    auto it = values_.find(key);
    return it == values_.end() ? 0 : it->second.size();
}

int32_t Preferences::getInt(const char* key, int32_t default_value) {
    String value = getString(key, String());
    if (value.isEmpty()) return default_value;
    return (int32_t)strtol(value.c_str(), nullptr, 10);
}

uint32_t Preferences::getUInt(const char* key, uint32_t default_value) {
    String value = getString(key, String());
    if (value.isEmpty()) return default_value;
    return (uint32_t)strtoul(value.c_str(), nullptr, 10);
}

uint32_t Preferences::getULong(const char* key, uint32_t default_value) {
    return getUInt(key, default_value);
}

uint64_t Preferences::getULong64(const char* key, uint64_t default_value) {
    String value = getString(key, String());
    if (value.isEmpty()) return default_value;
    return strtoull(value.c_str(), nullptr, 10);
}

float Preferences::getFloat(const char* key, float default_value) {
    String value = getString(key, String());
    if (value.isEmpty()) return default_value;
    return strtof(value.c_str(), nullptr);
}

bool Preferences::getBool(const char* key, bool default_value) {
    String value = getString(key, String());
    if (value.isEmpty()) return default_value;
    return value.c_str()[0] != '0';
}

String Preferences::getString(const char* key, const String& default_value) {
    if (!open_ || !key) return default_value;

    std::lock_guard<std::mutex> lock(store_mutex());
    auto it = values_.find(key);
    if (it == values_.end()) return default_value;
    return String(it->second);
}

bool Preferences::isKey(const char* key) {
    if (!open_ || !key) return false;

    std::lock_guard<std::mutex> lock(store_mutex());
    return values_.find(key) != values_.end();
}

bool Preferences::remove(const char* key) {
    if (!open_ || read_only_ || !key) return false;

    std::lock_guard<std::mutex> lock(store_mutex());
    if (values_.erase(key) == 0) return false;
    store();
    return true;
}

bool Preferences::clear() {
    if (!open_ || read_only_) return false;

    std::lock_guard<std::mutex> lock(store_mutex());
    values_.clear();
    store();
    return true;
}

/** Factory reset: drops every namespace, as erasing the NVS partition does. */
esp_err_t nvs_flash_erase() {
    std::lock_guard<std::mutex> lock(store_mutex());
    std::error_code ec;
    fs::remove_all(sim_nvs_dir(), ec);
    return ec ? ESP_FAIL : ESP_OK;
}
