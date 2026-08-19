/**
 * BluetoothManager, OTAHandler and DataStreamManager for the host.
 *
 * There is no radio to drive, so the manager reports a disabled, disconnected
 * adapter and the UI's Bluetooth pages render their real "off" states. OTA and
 * data export are inert: both are transports to a paired Mac, which the
 * simulator already is.
 */

#include "bluetooth/manager.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstring>

BluetoothManager::BluetoothManager()
    : ble_server(nullptr),
      ota_service(nullptr),
      data_service(nullptr),
      debug_service(nullptr),
      sysinfo_service(nullptr),
      ota_data_characteristic(nullptr),
      ota_control_characteristic(nullptr),
      ota_status_characteristic(nullptr),
      build_number_characteristic(nullptr),
      firmware_id_characteristic(nullptr),
      data_control_characteristic(nullptr),
      data_transfer_characteristic(nullptr),
      data_status_characteristic(nullptr),
      debug_rx_characteristic(nullptr),
      debug_tx_characteristic(nullptr),
      sysinfo_system_characteristic(nullptr),
      sysinfo_performance_characteristic(nullptr),
      sysinfo_hardware_characteristic(nullptr),
      sysinfo_sessions_characteristic(nullptr),
      sysinfo_diagnostics_characteristic(nullptr),
      device_connected(false),
      ble_enabled(false),
      always_on(false),
      debug_stream_active(false),
      enable_time(0),
      timeout_ms(0),
      last_disconnect_time(0),
      data_export_in_progress(false),
      data_status(BLE_DATA_IDLE),
      current_chunk(0),
      next_chunk_time(0),
      current_file_session_id(0),
      sessions_info_dirty(false),
      last_session_storage_version(0),
      last_reported_export_state(false),
      ui_status_callback(nullptr),
      ui_status_queue(nullptr),
      diagnostic_report_pending(false),
      diagnostic_report_in_progress(false),
      ota_complete_pending(false),
      ota_finalizing(false),
      conn_handle(0),
      connect_time(0),
      last_link_param_request(0),
      link_param_attempts(0) {}

BluetoothManager::~BluetoothManager() = default;

void BluetoothManager::init(Preferences* prefs) {
    (void)prefs;
    LOG_BLE("[SIM] Bluetooth disabled - no radio on the host\n");
}

void BluetoothManager::enable(unsigned long timeout) { (void)timeout; }
void BluetoothManager::enable_during_bootup() {}
void BluetoothManager::disable() {}
void BluetoothManager::handle() {}
void BluetoothManager::start_advertising() {}
void BluetoothManager::stop_advertising() {}

unsigned long BluetoothManager::get_remaining_time_ms() const { return 0; }
unsigned long BluetoothManager::get_bluetooth_timeout_remaining_ms() const { return 0; }

void BluetoothManager::set_always_on(bool enabled) { always_on = enabled; }

void BluetoothManager::start_data_export() {}
void BluetoothManager::stop_data_export() {}
void BluetoothManager::update_data_export() {}
float BluetoothManager::get_data_export_progress() const { return 0.0f; }
uint32_t BluetoothManager::get_data_export_session_count() const { return 0; }

void BluetoothManager::log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void BluetoothManager::set_ui_status_callback(UIStatusCallback callback) {
    ui_status_callback = callback;
}

void BluetoothManager::refresh_system_info() {}

String BluetoothManager::check_ota_failure_after_boot() { return String(); }

bool BluetoothManager::dequeue_ui_status(char* out, size_t out_len) {
    (void)out;
    (void)out_len;
    return false;
}

void BluetoothManager::onConnect(BLEServer* server) { (void)server; }
void BluetoothManager::onConnect(BLEServer* server, ble_gap_conn_desc* desc) { (void)server; (void)desc; }
void BluetoothManager::onDisconnect(BLEServer* server) { (void)server; }
void BluetoothManager::onWrite(BLECharacteristic* characteristic) { (void)characteristic; }
void BluetoothManager::onRead(BLECharacteristic* characteristic) { (void)characteristic; }

OTAHandler::OTAHandler()
    : ota_in_progress(false),
      patch_size(0),
      received_size(0),
      current_status(BLE_OTA_IDLE),
      is_full_update(false),
      failure_pending(false),
      last_error{0},
      preferences(nullptr),
      power_state(NORMAL_POWER),
      normal_cpu_freq_mhz(240),
      patch_writer{} {}

OTAHandler::~OTAHandler() = default;

float OTAHandler::get_progress() const { return 0.0f; }

bool OTAHandler::take_failure(String& expected_build) {
    expected_build = String();
    return false;
}

DataStreamManager::DataStreamManager()
    : current_session_id(0), file_bytes_sent(0), file_total_size(0), file_stream_active(false) {}

DataStreamManager::~DataStreamManager() = default;
