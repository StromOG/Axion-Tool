#pragma once
#include <esp_wifi.h>
#include <WiFi.h>

#define MAX_DISCOVERED_APS 30

struct DiscoveredAP {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t security; // 0=Open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3
    uint32_t lastSeenMs;
};

struct WifiStats {
    uint32_t totalPackets;
    uint32_t beaconCount;
    uint32_t probeCount;
    uint32_t deauthCount;
    uint32_t eapolCount;
    uint32_t pmkidCount;
    uint32_t dataCount;
    uint32_t mgmtCount;
    uint32_t attacksSent;
};

enum WifiAttackMode {
    WIFI_ATTACK_NONE = 0,
    WIFI_ATTACK_BEACON_SPAM,
    WIFI_ATTACK_RICKROLL,
    WIFI_ATTACK_DEAUTH_BROADCAST,
    WIFI_ATTACK_EAPOL_ROGUE
};

class WiFiAnalyzer {
public:
    static bool init();
    static void deinit();
    static bool isActive();

    static void startPromiscuous();
    static void stopPromiscuous();
    static void setChannel(uint8_t ch);
    static uint8_t getChannel();
    static void enableHop(bool enabled);

    static const WifiStats& getStats();
    static void resetStats();

    // AP List
    static uint8_t getAPCount();
    static bool getAP(uint8_t index, DiscoveredAP& outAp);
    static void clearAPs();

    // PCAP streaming
    static bool startPCAP(const char* filepath);
    static void stopPCAP();
    static bool isPCAPActive();

    // Active Generation (Adapted from Marauder)
    static void startAttack(WifiAttackMode mode);
    static void stopAttack();
    static bool isAttacking();
    static WifiAttackMode getAttackMode();
    static void sendBeacon(const char* ssid, uint8_t channel);
    static void sendDeauthBroadcast(const uint8_t* bssid, uint8_t channel);
    static void sendTargetedDeauth(const uint8_t* bssid, uint8_t channel);
    static void sendEapolFrame(const uint8_t* bssid, uint8_t channel);
    static void attackLoop(); // Periodic frame generation hook

    // Handshake Capture Status
    static bool hasNewHandshake();
    static const char* getLastHandshakeSSID();
    static void clearHandshakeFlag();

private:
    static void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    static void hopTask(void* param);

    static bool active;
    static bool hopEnabled;
    static uint8_t currentChannel;
    static WifiStats stats;
    static TaskHandle_t hopTaskHandle;
    static File pcapFile;
    static bool pcapActive;
    static uint32_t pcapCount;

    static WifiAttackMode currentAttack;
    static uint32_t lastAttackPacketMs;
};
