#pragma once

#include <Arduino.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <array>
#include <string>

class BLEControl {
public:
    BLEControl();
    void begin();
    void configure(const std::string& address, const std::string& service_uuid, const std::string& characteristic_uuid);
    bool sendColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness = 0x10);
    bool isConnected() const;
    void disconnect();

private:
    bool ensureConnection();
    bool writeColorCommand(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness);

    std::string device_address;
    BLEUUID service_uuid;
    BLEUUID characteristic_uuid;
    BLEClient* client;
    BLERemoteCharacteristic* characteristic;
    bool connected;

    static constexpr size_t kCommandLength = 9;
    static constexpr uint8_t kStartByte = 0x7E;
    static constexpr uint8_t kEndByte = 0xEF;
};

inline BLEControl::BLEControl()
    : client(nullptr), characteristic(nullptr), connected(false) {}

inline void BLEControl::begin() {
    BLEDevice::init("ESP32-LED-Bridge");
}

inline void BLEControl::configure(const std::string& address, const std::string& service_uuid, const std::string& characteristic_uuid) {
    if (connected && client && device_address != address) {
        disconnect();
    }
    device_address = address;
    this->service_uuid = BLEUUID(service_uuid.c_str());
    this->characteristic_uuid = BLEUUID(characteristic_uuid.c_str());
}

inline bool BLEControl::ensureConnection() {
    if (connected && client && client->isConnected()) {
        return true;
    }

    if (!client) {
        client = BLEDevice::createClient();
    }

    BLEAddress target(device_address.c_str());
    if (!client->connect(target)) {
        return false;
    }

    BLERemoteService* service = client->getService(service_uuid);
    if (!service) {
        disconnect();
        return false;
    }

    characteristic = service->getCharacteristic(characteristic_uuid);
    if (!characteristic) {
        disconnect();
        return false;
    }

    connected = true;
    return true;
}

inline bool BLEControl::sendColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness) {
    if (!ensureConnection()) {
        return false;
    }
    return writeColorCommand(red, green, blue, brightness);
}

inline bool BLEControl::writeColorCommand(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness) {
    std::array<uint8_t, kCommandLength> command = {
        kStartByte,
        0x07,
        0x05,
        0x03,
        red,
        green,
        blue,
        brightness,
        kEndByte
    };

    if (!characteristic) {
        return false;
    }

    characteristic->writeValue(command.data(), command.size(), true);
    return true;
}

inline bool BLEControl::isConnected() const {
    return connected && client && client->isConnected();
}

inline void BLEControl::disconnect() {
    if (client && client->isConnected()) {
        client->disconnect();
    }
    connected = false;
    characteristic = nullptr;
}
