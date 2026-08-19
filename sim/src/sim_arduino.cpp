/**
 * Arduino core and ESP-IDF runtime services, implemented on host primitives.
 */

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <nvs_flash.h>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <random>
#include <thread>

#include <mach/mach.h>
#include <sys/sysctl.h>

SimSerial Serial;
SimESPClass ESP;

namespace {

std::chrono::steady_clock::time_point boot_time = std::chrono::steady_clock::now();
std::mutex serial_mutex;

std::mt19937& rng() {
    static thread_local std::mt19937 generator(std::random_device{}());
    return generator;
}

uint64_t host_free_bytes() {
    vm_size_t page_size = 0;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) return 0;

    vm_statistics64_data_t stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    kern_return_t result = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                             reinterpret_cast<host_info64_t>(&stats), &count);
    if (result != KERN_SUCCESS) return 0;

    return static_cast<uint64_t>(stats.free_count + stats.inactive_count) * page_size;
}

uint64_t host_total_bytes() {
    uint64_t total = 0;
    size_t length = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &length, nullptr, 0) != 0) return 0;
    return total;
}

uint32_t clamp_to_u32(uint64_t value) {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}

}  // namespace

unsigned long millis() {
    auto elapsed = std::chrono::steady_clock::now() - boot_time;
    return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

unsigned long micros() {
    auto elapsed = std::chrono::steady_clock::now() - boot_time;
    return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void delayMicroseconds(unsigned int us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void yield() {
    std::this_thread::yield();
}

void pinMode(int pin, int mode) { (void)pin; (void)mode; }
void digitalWrite(int pin, int value) { (void)pin; (void)value; }
int digitalRead(int pin) { (void)pin; return LOW; }

long random(long max_exclusive) {
    if (max_exclusive <= 0) return 0;
    return static_cast<long>(rng()() % static_cast<unsigned long>(max_exclusive));
}

long random(long min_inclusive, long max_exclusive) {
    if (max_exclusive <= min_inclusive) return min_inclusive;
    return min_inclusive + random(max_exclusive - min_inclusive);
}

void randomSeed(unsigned long seed) {
    rng().seed(static_cast<uint32_t>(seed));
}

String::String(float value, int decimals) : String(static_cast<double>(value), decimals) {}

String::String(double value, int decimals) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    buf_ = buffer;
}

int SimSerial::printf(const char* format, ...) {
    std::lock_guard<std::mutex> lock(serial_mutex);
    va_list args;
    va_start(args, format);
    int written = vfprintf(stdout, format, args);
    va_end(args);
    return written;
}

void SimSerial::print(const char* s) {
    std::lock_guard<std::mutex> lock(serial_mutex);
    fputs(s, stdout);
}

void SimSerial::print(int v) {
    std::lock_guard<std::mutex> lock(serial_mutex);
    fprintf(stdout, "%d", v);
}

void SimSerial::println() {
    std::lock_guard<std::mutex> lock(serial_mutex);
    fputc('\n', stdout);
}

void SimSerial::println(const char* s) {
    std::lock_guard<std::mutex> lock(serial_mutex);
    fputs(s, stdout);
    fputc('\n', stdout);
}

void SimSerial::flush() {
    std::lock_guard<std::mutex> lock(serial_mutex);
    fflush(stdout);
}

/* Heap figures come from the host, so they read as host memory rather than
 * pretending to be the ESP32's 512 kB of internal RAM. */
uint32_t SimESPClass::getFreeHeap() const { return clamp_to_u32(host_free_bytes()); }
uint32_t SimESPClass::getHeapSize() const { return clamp_to_u32(host_total_bytes()); }
uint32_t SimESPClass::getMinFreeHeap() const { return getFreeHeap(); }
uint32_t SimESPClass::getFreePsram() const { return getFreeHeap(); }

void SimESPClass::restart() {
    Serial.printf("[SIM] Restart requested - exiting\n");
    Serial.flush();
    std::exit(0);
}

int64_t esp_timer_get_time() {
    auto elapsed = std::chrono::steady_clock::now() - boot_time;
    return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        default: return "ESP_ERR_UNKNOWN";
    }
}

esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
void esp_restart() { ESP.restart(); }
uint32_t esp_get_free_heap_size() { return ESP.getFreeHeap(); }
uint32_t esp_get_minimum_free_heap_size() { return ESP.getMinFreeHeap(); }
const char* esp_get_idf_version() { return "sim-host"; }
uint32_t esp_random() { return static_cast<uint32_t>(rng()()); }

void* heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
void* heap_caps_calloc(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) { (void)caps; return realloc(ptr, size); }

void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    (void)caps;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (size % alignment != 0) size += alignment - (size % alignment);
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
}

void heap_caps_free(void* ptr) { free(ptr); }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return ESP.getFreeHeap(); }
size_t heap_caps_get_total_size(uint32_t caps) { (void)caps; return ESP.getHeapSize(); }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return ESP.getFreeHeap(); }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return ESP.getFreeHeap(); }

esp_err_t esp_task_wdt_add(TaskHandle_t task) { (void)task; return ESP_OK; }
esp_err_t esp_task_wdt_delete(TaskHandle_t task) { (void)task; return ESP_OK; }
esp_err_t esp_task_wdt_reset() { return ESP_OK; }
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* config) { (void)config; return ESP_OK; }

esp_err_t nvs_flash_init() { return ESP_OK; }
esp_err_t nvs_flash_deinit() { return ESP_OK; }
