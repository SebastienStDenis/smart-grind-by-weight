/**
 * Host stand-in for the ESP32 WiFi station API.
 *
 * The Mac is already on a network, so "connecting" is instant and always
 * succeeds; the state machine in TrainDataClient still runs unchanged.
 */
#pragma once

#include <cstdint>
#include <functional>

#include "Arduino.h"

typedef enum {
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_DISCONNECTED = 6
} wl_status_t;

typedef enum {
    WIFI_OFF = 0,
    WIFI_STA,
    WIFI_AP,
    WIFI_AP_STA
} wifi_mode_t;

typedef enum {
    WIFI_POWER_19_5dBm = 78,
    WIFI_POWER_19dBm = 76,
    WIFI_POWER_18_5dBm = 74,
    WIFI_POWER_17dBm = 68,
    WIFI_POWER_15dBm = 60,
    WIFI_POWER_13dBm = 52,
    WIFI_POWER_11dBm = 44,
    WIFI_POWER_8_5dBm = 34,
    WIFI_POWER_7dBm = 28,
    WIFI_POWER_5dBm = 20,
    WIFI_POWER_2dBm = 8,
    WIFI_POWER_MINUS_1dBm = -4
} wifi_power_t;

typedef enum {
    ARDUINO_EVENT_WIFI_STA_CONNECTED = 4,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5,
    ARDUINO_EVENT_WIFI_STA_GOT_IP = 7
} arduino_event_id_t;

typedef arduino_event_id_t WiFiEvent_t;

typedef struct {
    struct {
        uint8_t reason;
    } wifi_sta_disconnected;
} WiFiEventInfo_t;

class IPAddress {
public:
    IPAddress() = default;
    explicit IPAddress(const String& text) : text_(text) {}
    String toString() const { return text_; }

private:
    String text_ = "0.0.0.0";
};

class SimWiFiClass {
public:
    using EventCallback = std::function<void(WiFiEvent_t, WiFiEventInfo_t)>;

    bool mode(wifi_mode_t mode);
    wl_status_t begin(const char* ssid, const char* password);
    bool disconnect(bool wifi_off = false, bool erase_ap = false);
    bool isConnected() const;
    wl_status_t status() const;
    IPAddress localIP() const;
    void setAutoReconnect(bool enable) { (void)enable; }
    bool setTxPower(wifi_power_t power) { (void)power; return true; }
    void onEvent(EventCallback callback);

private:
    void emit(WiFiEvent_t event, uint8_t reason = 0);

    EventCallback callback_ = nullptr;
    bool connected_ = false;
};

extern SimWiFiClass WiFi;
