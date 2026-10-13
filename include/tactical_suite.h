#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

// ============================================================
// 1. DRONE REMOTE-ID RADAR (FAA OpenDroneID / ASTM F3411)
// ============================================================
#define MAX_TRACKED_DRONES 12

struct DroneTarget {
    uint8_t mac[6];
    char uasId[24];         // Serial Number / ID
    char operatorId[24];     // Operator / Pilot Registration
    uint8_t uaType;         // 1=Plane, 2=Multirotor, 3=Helicopter, 4=VTOL, 0=Unknown
    int32_t latitudeE7;     // Degrees * 1e7
    int32_t longitudeE7;    // Degrees * 1e7
    float altitudeGeoM;     // Height above sea level
    float heightM;          // Height AGL (above ground level)
    float speedMps;         // Horizontal ground speed m/s
    uint16_t headingDeg;    // 0-359 degrees
    int32_t pilotLatE7;     // Pilot Latitude * 1e7
    int32_t pilotLonE7;     // Pilot Longitude * 1e7
    int8_t rssi;
    uint32_t lastSeenMs;
    uint8_t channel;
    bool hasLocation;
    bool hasPilot;
};

// ============================================================
// 2. SIGNAL FOX-HUNT DIRECTION SEEKER (Marauder RSSI Hunter)
// ============================================================
enum FoxTargetType {
    FOX_TYPE_WIFI_AP = 0,
    FOX_TYPE_WIFI_CLIENT,
    FOX_TYPE_BLE_DEVICE,
    FOX_TYPE_AIRTAG,
    FOX_TYPE_FLIPPER,
    FOX_TYPE_SKIMMER
};

struct FoxHuntSession {
    uint8_t targetMac[6];
    char targetName[32];
    FoxTargetType targetType;
    int8_t currentRssi;
    float filteredRssi;
    int8_t peakRssi;
    uint8_t channel;
    uint32_t lastSeenMs;
    bool isLocked;
    float estimatedDistanceM; // Computed Free-Space Path Loss estimate
    uint32_t lastClickMs;     // Audio Geiger-counter pulse timer
};

// ============================================================
// 3. COVERT SKIMMER & SURVEILLANCE SCANNER (Marauder Defenses)
// ============================================================
#define MAX_SURVEILLANCE_TARGETS 16

enum SurveillanceType {
    SURV_SKIMMER_HC05 = 0,   // ATM / Gas Pump Bluetooth Skimmer (HC-03/05/06)
    SURV_SKIMMER_RN42,       // Microchip / Roving Networks Skimmer
    SURV_FLOCK_ALPR,         // Flock Safety License Plate Reader Camera
    SURV_AIRTAG_STALKER,     // Apple AirTag / FindMy Proximity Beacon
    SURV_FLIPPER_ZERO,       // Flipper Zero Offensive Transceiver
    SURV_PWNAGOTCHI          // Pwnagotchi AI Wi-Fi Deauther
};

enum ThreatLevel {
    THREAT_INFO = 0,         // Blue / Info
    THREAT_ELEVATED,         // Amber / Medium
    THREAT_CRITICAL          // Crimson / High Danger
};

struct SurveillanceNode {
    uint8_t mac[6];
    char name[32];
    SurveillanceType type;
    ThreatLevel threat;
    int8_t rssi;
    uint8_t channel;
    uint32_t firstSeenMs;
    uint32_t lastSeenMs;
    uint16_t packetCount;
};

// ============================================================
// TACTICAL SUITE CONTROLLER
// ============================================================
class TacticalSuite {
public:
    static void init();

    // Drone Remote-ID
    static void startDroneScan();
    static void stopDroneScan();
    static uint8_t getDroneCount();
    static bool getDrone(uint8_t index, DroneTarget& outDrone);
    static void feedDroneWifiFrame(const uint8_t* frame, size_t len, int8_t rssi, uint8_t channel);
    static void feedDroneBlePayload(const uint8_t* payload, size_t len, const uint8_t mac[6], int8_t rssi);
    static void updateDroneSimLoop(); // Procedural airspace simulation for testing

    // Fox-Hunt Seeker
    static void setFoxTarget(const uint8_t mac[6], const char* name, FoxTargetType type, uint8_t ch = 1);
    static void clearFoxTarget();
    static const FoxHuntSession& getFoxSession();
    static void updateFoxRssi(const uint8_t mac[6], int8_t rssi, uint8_t ch = 0);
    static bool checkGeigerChirp(uint16_t& outFreq, uint16_t& outDur); // Returns true if click due

    // Skimmer & Surveillance Scanner
    static void startSurveillanceScan();
    static void stopSurveillanceScan();
    static uint8_t getSurveillanceCount();
    static bool getSurveillanceNode(uint8_t index, SurveillanceNode& outNode);
    static void checkBleDeviceForSurveillance(const uint8_t mac[6], const char* name, const uint8_t* mfgData, size_t mfgLen, int8_t rssi);
    static void checkWifiAPForSurveillance(const uint8_t bssid[6], const char* ssid, uint8_t channel, int8_t rssi);
    static void updateSurveillanceSimLoop(); // Procedural nodes if lab testing
};
