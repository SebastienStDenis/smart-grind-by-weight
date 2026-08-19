/**
 * Host stand-in for the ESP32 Preferences (NVS) API.
 *
 * Each namespace is a file under the simulator data directory, so settings,
 * targets and WiFi credentials survive a restart exactly as they do on NVS.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include "Arduino.h"

class Preferences {
public:
    bool begin(const char* name, bool read_only = false);
    void end();

    size_t putInt(const char* key, int32_t value);
    size_t putUInt(const char* key, uint32_t value);
    size_t putULong(const char* key, uint32_t value);
    size_t putULong64(const char* key, uint64_t value);
    size_t putFloat(const char* key, float value);
    size_t putBool(const char* key, bool value);
    size_t putString(const char* key, const char* value);
    size_t putString(const char* key, const String& value);
    size_t putBytes(const char* key, const void* value, size_t length);

    int32_t getInt(const char* key, int32_t default_value = 0);
    uint32_t getUInt(const char* key, uint32_t default_value = 0);
    uint32_t getULong(const char* key, uint32_t default_value = 0);
    uint64_t getULong64(const char* key, uint64_t default_value = 0);
    float getFloat(const char* key, float default_value = 0.0f);
    bool getBool(const char* key, bool default_value = false);
    String getString(const char* key, const String& default_value = String());
    size_t getBytes(const char* key, void* buffer, size_t max_length);
    size_t getBytesLength(const char* key);

    bool isKey(const char* key);
    bool remove(const char* key);
    bool clear();

private:
    void load();
    void store();

    std::string namespace_;
    std::map<std::string, std::string> values_;
    bool open_ = false;
    bool read_only_ = false;
};
