/**
 * Host stand-in for the Arduino FS/LittleFS API, backed by a directory on disk.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "Arduino.h"

class SimFileImpl;

class File {
public:
    File() = default;
    explicit File(std::shared_ptr<SimFileImpl> impl) : impl_(std::move(impl)) {}

    explicit operator bool() const;
    void close();
    size_t size() const;
    size_t position() const;
    bool seek(size_t pos);
    int available();
    int read();
    size_t read(uint8_t* buffer, size_t length);
    size_t readBytes(char* buffer, size_t length);
    size_t write(const uint8_t* buffer, size_t length);
    size_t write(uint8_t value);
    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));
    size_t println();
    size_t println(const char* text);
    size_t println(const String& text) { return println(text.c_str()); }
    size_t print(const char* text);
    void flush();
    const char* name() const;
    const char* path() const;
    bool isDirectory() const;
    File openNextFile();

private:
    std::shared_ptr<SimFileImpl> impl_;
};

class SimFileSystem {
public:
    /** Roots the filesystem at `root`; created if missing. */
    void set_root(const std::string& root) { root_ = root; }
    const std::string& root() const { return root_; }

    bool begin(bool format_on_fail = false, const char* base_path = "", uint8_t max_files = 10);
    void end();
    bool exists(const char* path);
    bool mkdir(const char* path);
    bool rmdir(const char* path);
    bool remove(const char* path);
    File open(const char* path, const char* mode = "r");
    File open(const String& path, const char* mode = "r") { return open(path.c_str(), mode); }
    size_t totalBytes();
    size_t usedBytes();

    /** Maps a device path such as "/grind_sessions" onto the host directory. */
    std::string host_path(const char* path) const;

private:
    std::string root_ = "sim_data/littlefs";
};

extern SimFileSystem LittleFS;
