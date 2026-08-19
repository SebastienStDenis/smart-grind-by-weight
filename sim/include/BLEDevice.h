/**
 * Host stand-in for the NimBLE Arduino API.
 *
 * Only the declarations BluetoothManager's header needs are present; the
 * simulator's BluetoothManager (sim_bluetooth.cpp) never talks to a radio.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "host/ble_gap.h"

class BLECharacteristic;
class BLEService;
class BLEServer;
class BLEAdvertising;

class BLECharacteristic {
public:
    void setValue(const uint8_t* data, size_t length) { (void)data; (void)length; }
    void setValue(const std::string& value) { (void)value; }
    void notify() {}
    uint8_t* getData() { return nullptr; }
    size_t getLength() const { return 0; }
    std::string getValue() const { return std::string(); }
    std::string getUUID() const { return std::string(); }
};

class BLEService {
public:
    BLECharacteristic* createCharacteristic(const char* uuid, uint32_t properties) {
        (void)uuid;
        (void)properties;
        return nullptr;
    }
    void start() {}
};

class BLEServerCallbacks {
public:
    virtual ~BLEServerCallbacks() = default;
    virtual void onConnect(BLEServer* server) { (void)server; }
    virtual void onConnect(BLEServer* server, ble_gap_conn_desc* desc) { (void)server; (void)desc; }
    virtual void onDisconnect(BLEServer* server) { (void)server; }
};

class BLECharacteristicCallbacks {
public:
    virtual ~BLECharacteristicCallbacks() = default;
    virtual void onWrite(BLECharacteristic* characteristic) { (void)characteristic; }
    virtual void onRead(BLECharacteristic* characteristic) { (void)characteristic; }
};

class BLEServer {
public:
    void setCallbacks(BLEServerCallbacks* callbacks) { (void)callbacks; }
    BLEService* createService(const char* uuid) { (void)uuid; return nullptr; }
    BLEAdvertising* getAdvertising() { return nullptr; }
};

class BLEAdvertising {
public:
    void start() {}
    void stop() {}
};

class BLEDeviceClass {
public:
    static void init(const std::string& name) { (void)name; }
    static void deinit(bool clear_all = false) { (void)clear_all; }
    static BLEServer* createServer() { return nullptr; }
    static BLEAdvertising* getAdvertising() { return nullptr; }
    static void setMTU(uint16_t mtu) { (void)mtu; }
};

typedef BLEDeviceClass BLEDevice;

#define BLE_PROPERTY_READ    (1 << 0)
#define BLE_PROPERTY_WRITE   (1 << 1)
#define BLE_PROPERTY_NOTIFY  (1 << 4)
