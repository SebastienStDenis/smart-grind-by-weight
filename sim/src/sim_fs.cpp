/**
 * LittleFS on top of a host directory.
 *
 * Device paths ("/grind_sessions/42.bin") map onto files under the simulator
 * data directory, so grind logs and exports behave like the real flash volume.
 */

#include <FS.h>

#include <algorithm>
#include <cstdarg>
#include <filesystem>
#include <string>
#include <vector>

#include "sim_paths.h"

namespace fs = std::filesystem;

SimFileSystem LittleFS;

/** One open file or directory handle. */
class SimFileImpl {
public:
    SimFileImpl(FILE* handle, std::string device_path)
        : handle_(handle), device_path_(std::move(device_path)) {
        name_ = fs::path(device_path_).filename().string();
    }

    SimFileImpl(std::vector<std::string> entries, std::string device_path)
        : is_directory_(true), entries_(std::move(entries)), device_path_(std::move(device_path)) {
        name_ = fs::path(device_path_).filename().string();
    }

    ~SimFileImpl() { close(); }

    void close() {
        if (handle_) {
            fclose(handle_);
            handle_ = nullptr;
        }
    }

    bool valid() const { return handle_ != nullptr || is_directory_; }
    bool is_directory() const { return is_directory_; }
    FILE* handle() const { return handle_; }
    const std::string& name() const { return name_; }
    const std::string& device_path() const { return device_path_; }

    /** Next directory entry, or an empty string when exhausted. */
    std::string next_entry() {
        if (next_index_ >= entries_.size()) return std::string();
        return entries_[next_index_++];
    }

private:
    FILE* handle_ = nullptr;
    bool is_directory_ = false;
    std::vector<std::string> entries_;
    size_t next_index_ = 0;
    std::string device_path_;
    std::string name_;
};

File::operator bool() const {
    return impl_ && impl_->valid();
}

void File::close() {
    if (impl_) impl_->close();
    impl_.reset();
}

size_t File::size() const {
    if (!impl_ || !impl_->handle()) return 0;

    long current = ftell(impl_->handle());
    fseek(impl_->handle(), 0, SEEK_END);
    long end = ftell(impl_->handle());
    fseek(impl_->handle(), current, SEEK_SET);
    return end < 0 ? 0 : (size_t)end;
}

size_t File::position() const {
    if (!impl_ || !impl_->handle()) return 0;
    long pos = ftell(impl_->handle());
    return pos < 0 ? 0 : (size_t)pos;
}

bool File::seek(size_t pos) {
    if (!impl_ || !impl_->handle()) return false;
    return fseek(impl_->handle(), (long)pos, SEEK_SET) == 0;
}

int File::available() {
    if (!impl_ || !impl_->handle()) return 0;
    size_t total = size();
    size_t pos = position();
    return pos >= total ? 0 : (int)(total - pos);
}

int File::read() {
    if (!impl_ || !impl_->handle()) return -1;
    return fgetc(impl_->handle());
}

size_t File::read(uint8_t* buffer, size_t length) {
    if (!impl_ || !impl_->handle() || !buffer) return 0;
    return fread(buffer, 1, length, impl_->handle());
}

size_t File::readBytes(char* buffer, size_t length) {
    return read(reinterpret_cast<uint8_t*>(buffer), length);
}

size_t File::write(const uint8_t* buffer, size_t length) {
    if (!impl_ || !impl_->handle() || !buffer) return 0;
    return fwrite(buffer, 1, length, impl_->handle());
}

size_t File::write(uint8_t value) {
    return write(&value, 1);
}

size_t File::printf(const char* format, ...) {
    if (!impl_ || !impl_->handle()) return 0;

    va_list args;
    va_start(args, format);
    int written = vfprintf(impl_->handle(), format, args);
    va_end(args);
    return written < 0 ? 0 : (size_t)written;
}

size_t File::print(const char* text) {
    if (!impl_ || !impl_->handle() || !text) return 0;
    return fwrite(text, 1, strlen(text), impl_->handle());
}

size_t File::println() {
    if (!impl_ || !impl_->handle()) return 0;
    return fwrite("\n", 1, 1, impl_->handle());
}

size_t File::println(const char* text) {
    size_t written = print(text);
    if (impl_ && impl_->handle()) written += fwrite("\n", 1, 1, impl_->handle());
    return written;
}

void File::flush() {
    if (impl_ && impl_->handle()) fflush(impl_->handle());
}

const char* File::name() const {
    return impl_ ? impl_->name().c_str() : "";
}

const char* File::path() const {
    return impl_ ? impl_->device_path().c_str() : "";
}

bool File::isDirectory() const {
    return impl_ && impl_->is_directory();
}

File File::openNextFile() {
    if (!impl_ || !impl_->is_directory()) return File();

    std::string entry = impl_->next_entry();
    if (entry.empty()) return File();

    return LittleFS.open(entry.c_str(), "r");
}

std::string SimFileSystem::host_path(const char* path) const {
    std::string device_path = path ? path : "/";
    while (!device_path.empty() && device_path.front() == '/') {
        device_path.erase(device_path.begin());
    }
    return (fs::path(root_) / device_path).string();
}

bool SimFileSystem::begin(bool format_on_fail, const char* base_path, uint8_t max_files) {
    (void)format_on_fail;
    (void)base_path;
    (void)max_files;

    root_ = sim_littlefs_dir();
    std::error_code ec;
    fs::create_directories(root_, ec);
    return !ec;
}

void SimFileSystem::end() {}

bool SimFileSystem::exists(const char* path) {
    std::error_code ec;
    return fs::exists(host_path(path), ec);
}

bool SimFileSystem::mkdir(const char* path) {
    std::error_code ec;
    fs::create_directories(host_path(path), ec);
    return !ec;
}

bool SimFileSystem::rmdir(const char* path) {
    std::error_code ec;
    return fs::remove(host_path(path), ec);
}

bool SimFileSystem::remove(const char* path) {
    std::error_code ec;
    return fs::remove(host_path(path), ec);
}

File SimFileSystem::open(const char* path, const char* mode) {
    if (!path) return File();

    std::string host = host_path(path);
    std::error_code ec;

    if (fs::is_directory(host, ec)) {
        std::string device_root = path;
        if (device_root.size() > 1 && device_root.back() == '/') device_root.pop_back();

        std::vector<std::string> entries;
        for (const auto& entry : fs::directory_iterator(host, ec)) {
            entries.push_back(device_root + "/" + entry.path().filename().string());
        }
        std::sort(entries.begin(), entries.end());
        return File(std::make_shared<SimFileImpl>(std::move(entries), device_root));
    }

    /* Writes create any missing parent directories, matching LittleFS.open("w"). */
    if (mode && (*mode == 'w' || *mode == 'a')) {
        fs::create_directories(fs::path(host).parent_path(), ec);
    }

    FILE* handle = fopen(host.c_str(), mode ? mode : "r");
    if (!handle) return File();

    return File(std::make_shared<SimFileImpl>(handle, path));
}

size_t SimFileSystem::totalBytes() {
    return 6u * 1024u * 1024u;  // Matches the device's LittleFS partition
}

size_t SimFileSystem::usedBytes() {
    std::error_code ec;
    size_t used = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root_, ec)) {
        if (entry.is_regular_file(ec)) used += (size_t)entry.file_size(ec);
    }
    return used;
}
