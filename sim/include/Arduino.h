/**
 * Host stand-in for the Arduino core.
 *
 * Provides the small slice of the API the firmware actually uses: a monotonic
 * millisecond clock started at process launch, Serial routed to stdout, the
 * Arduino String type, and no-op GPIO calls.
 */
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

/* The ESP32 Arduino core makes these visible through Arduino.h, and firmware
 * sources rely on that. */
#include "esp_timer.h"

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM
#define F(x) (x)

typedef uint8_t byte;
typedef bool boolean;

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
long random(long max_exclusive);
long random(long min_inclusive, long max_exclusive);
void randomSeed(unsigned long seed);

template <typename T> constexpr const T& sim_min(const T& a, const T& b) { return a < b ? a : b; }
template <typename T> constexpr const T& sim_max(const T& a, const T& b) { return a > b ? a : b; }

template <typename T, typename U> constexpr auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename T, typename U> constexpr auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
template <typename T, typename L, typename H> constexpr T constrain(T v, L lo, H hi) {
    return v < static_cast<T>(lo) ? static_cast<T>(lo) : (v > static_cast<T>(hi) ? static_cast<T>(hi) : v);
}

/** Arduino's String, backed by std::string. */
class String {
public:
    String() = default;
    String(const char* s) : buf_(s ? s : "") {}
    String(const std::string& s) : buf_(s) {}
    String(char c) : buf_(1, c) {}
    explicit String(int v) : buf_(std::to_string(v)) {}
    explicit String(unsigned int v) : buf_(std::to_string(v)) {}
    explicit String(long v) : buf_(std::to_string(v)) {}
    explicit String(unsigned long v) : buf_(std::to_string(v)) {}
    explicit String(float v, int decimals = 2);
    explicit String(double v, int decimals = 2);

    const char* c_str() const { return buf_.c_str(); }
    size_t length() const { return buf_.length(); }
    bool isEmpty() const { return buf_.empty(); }
    void clear() { buf_.clear(); }
    char charAt(size_t i) const { return i < buf_.size() ? buf_[i] : '\0'; }
    bool startsWith(const String& p) const { return buf_.rfind(p.buf_, 0) == 0; }
    bool endsWith(const String& s) const {
        return buf_.size() >= s.buf_.size() && buf_.compare(buf_.size() - s.buf_.size(), s.buf_.size(), s.buf_) == 0;
    }
    int indexOf(const String& s) const {
        size_t p = buf_.find(s.buf_);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    int lastIndexOf(const String& s) const {
        size_t p = buf_.rfind(s.buf_);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    String substring(size_t from) const { return String(buf_.substr(sim_min(from, buf_.size()))); }
    String substring(size_t from, size_t to) const {
        from = sim_min(from, buf_.size());
        to = sim_min(to, buf_.size());
        return String(to > from ? buf_.substr(from, to - from) : std::string());
    }
    int toInt() const { return atoi(buf_.c_str()); }
    float toFloat() const { return strtof(buf_.c_str(), nullptr); }

    String& operator+=(const String& o) { buf_ += o.buf_; return *this; }
    friend String operator+(String a, const String& b) { a.buf_ += b.buf_; return a; }
    bool operator==(const String& o) const { return buf_ == o.buf_; }
    bool operator!=(const String& o) const { return buf_ != o.buf_; }
    operator const char*() const { return buf_.c_str(); }

private:
    std::string buf_;
};

/** Serial, wired to stdout. */
class SimSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void end() {}
    int printf(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void print(const char* s);
    void print(const String& s) { print(s.c_str()); }
    void print(int v);
    void println();
    void println(const char* s);
    void println(const String& s) { println(s.c_str()); }
    void flush();
    operator bool() const { return true; }
};

extern SimSerial Serial;

/** ESP runtime info, reported from host equivalents. */
class SimESPClass {
public:
    uint32_t getFreeHeap() const;
    uint32_t getHeapSize() const;
    uint32_t getMinFreeHeap() const;
    uint32_t getFlashChipSize() const { return 16u * 1024u * 1024u; }
    uint32_t getCpuFreqMHz() const { return 240; }
    uint32_t getPsramSize() const { return 8u * 1024u * 1024u; }
    uint32_t getFreePsram() const;
    const char* getChipModel() const { return "ESP32-S3 (simulated)"; }
    uint8_t getChipRevision() const { return 0; }
    uint64_t getEfuseMac() const { return 0x0102030405060708ull; }
    void restart();
};

extern SimESPClass ESP;
