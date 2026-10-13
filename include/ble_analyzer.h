#pragma once
#include <stdint.h>
#include <Arduino.h>

enum BleSpamType {
    BLE_SPAM_APPLE = 0,
    BLE_SPAM_GOOGLE,
    BLE_SPAM_SAMSUNG,
    BLE_SPAM_MICROSOFT,
    BLE_SPAM_FLIPPER,
    BLE_SPAM_ALL
};

struct BleDeviceRecord {
    uint8_t mac[6];
    char name[24];
    int8_t rssi;
    uint8_t deviceType; // 0=Unknown, 1=Apple, 2=Google, 3=Samsung, 4=Microsoft, 5=Flipper/Tracker
    uint32_t lastSeenMs;
};

class BLEAnalyzer {
public:
    static bool init();
    static void deinit();
    static bool isActive();

    // Scanning
    static void startScan();
    static void stopScan();
    static bool isScanning();
    static uint16_t getDeviceCount();
    static bool getDevice(uint16_t index, BleDeviceRecord& outDev);
    static void clearDevices();

    // Beacon / Pair Spammer from Marauder research
    static void startSpam(BleSpamType type);
    static void stopSpam();
    static bool isSpamming();
    static BleSpamType getSpamType();
    static uint32_t getPacketsSent();
    static void spamLoop(); // Housekeeping called in core 0/1 loop

private:
    static void sendAdvRaw(const uint8_t* data, uint8_t len);
    static void sendAppleAdv();
    static void sendGoogleAdv();
    static void sendSamsungAdv();
    static void sendMicrosoftAdv();
    static void sendFlipperAdv();

    static bool active;
    static bool scanning;
    static bool spamming;
    static BleSpamType currentSpamType;
    static uint32_t packetsSent;
    static uint32_t lastAdvMs;
};
