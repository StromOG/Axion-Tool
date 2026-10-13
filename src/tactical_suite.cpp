#include "tactical_suite.h"
#include <string.h>
#include <math.h>

// Storage containers
static DroneTarget s_drones[MAX_TRACKED_DRONES];
static uint8_t s_droneCount = 0;
static bool s_droneScanning = false;

static FoxHuntSession s_foxSession = {
    .targetMac = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .targetName = "NO TARGET LOCKED",
    .targetType = FOX_TYPE_WIFI_AP,
    .currentRssi = -100,
    .filteredRssi = -100.0f,
    .peakRssi = -100,
    .channel = 1,
    .lastSeenMs = 0,
    .isLocked = false,
    .estimatedDistanceM = 99.0f,
    .lastClickMs = 0
};

static SurveillanceNode s_survNodes[MAX_SURVEILLANCE_TARGETS];
static uint8_t s_survCount = 0;
static bool s_survScanning = false;

// ============================================================
// INITIALIZATION
// ============================================================
void TacticalSuite::init() {
    s_droneCount = 0;
    s_survCount = 0;
    clearFoxTarget();
}

// ============================================================
// 1. DRONE REMOTE-ID RADAR
// ============================================================
void TacticalSuite::startDroneScan() {
    s_droneScanning = true;
}

void TacticalSuite::stopDroneScan() {
    s_droneScanning = false;
}

uint8_t TacticalSuite::getDroneCount() {
    return s_droneCount;
}

bool TacticalSuite::getDrone(uint8_t index, DroneTarget& outDrone) {
    if (index >= s_droneCount) return false;
    outDrone = s_drones[index];
    return true;
}

static DroneTarget* findOrCreateDrone(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < s_droneCount; i++) {
        if (memcmp(s_drones[i].mac, mac, 6) == 0) {
            return &s_drones[i];
        }
    }
    if (s_droneCount < MAX_TRACKED_DRONES) {
        DroneTarget* d = &s_drones[s_droneCount++];
        memset(d, 0, sizeof(DroneTarget));
        memcpy(d->mac, mac, 6);
        snprintf(d->uasId, sizeof(d->uasId), "UAV-%02X%02X", mac[4], mac[5]);
        return d;
    }
    return &s_drones[0]; // Recycle oldest
}

void TacticalSuite::feedDroneWifiFrame(const uint8_t* frame, size_t len, int8_t rssi, uint8_t channel) {
    if (!frame || len < 36) return;
    // Wi-Fi Management Beacon/Probe Response: Look for OpenDroneID IE (Tag 0xDD, OUI 0xFA-0x0B-0xBC)
    const uint8_t* bssid = frame + 16;
    for (size_t offset = 36; offset + 6 < len; ) {
        uint8_t id = frame[offset];
        uint8_t elLen = frame[offset + 1];
        if (offset + 2 + elLen > len) break;
        if (id == 0xDD && elLen >= 7) {
            const uint8_t* oui = frame + offset + 2;
            if (oui[0] == 0xFA && oui[1] == 0x0B && oui[2] == 0xBC) {
                // OpenDroneID Element found
                DroneTarget* drone = findOrCreateDrone(bssid);
                drone->rssi = rssi;
                drone->channel = channel;
                drone->lastSeenMs = millis();
                // Decode payload messages
                const uint8_t* payload = oui + 4; // Skip OUI & type
                size_t pLen = elLen - 4;
                if (pLen >= 25) {
                    uint8_t msgType = (payload[0] >> 4) & 0x0F;
                    if (msgType == 0) { // Basic ID
                        drone->uaType = payload[1] & 0x0F;
                        char idBuf[21] = {0};
                        memcpy(idBuf, payload + 2, 20);
                        if (strlen(idBuf) > 3) strncpy(drone->uasId, idBuf, sizeof(drone->uasId) - 1);
                    } else if (msgType == 1) { // Location / Vector
                        drone->hasLocation = true;
                        drone->headingDeg = (uint16_t)payload[3] * 2;
                        drone->speedMps = (float)payload[4] * 0.25f;
                        int32_t lat = (int32_t)(payload[5] | (payload[6] << 8) | (payload[7] << 16) | (payload[8] << 24));
                        int32_t lon = (int32_t)(payload[9] | (payload[10] << 8) | (payload[11] << 16) | (payload[12] << 24));
                        drone->latitudeE7 = lat;
                        drone->longitudeE7 = lon;
                        uint16_t altRaw = (uint16_t)(payload[13] | (payload[14] << 8));
                        drone->altitudeGeoM = ((float)altRaw * 0.5f) - 1000.0f;
                    } else if (msgType == 4) { // System / Pilot Location
                        drone->hasPilot = true;
                        int32_t pLat = (int32_t)(payload[2] | (payload[3] << 8) | (payload[4] << 16) | (payload[5] << 24));
                        int32_t pLon = (int32_t)(payload[6] | (payload[7] << 8) | (payload[8] << 16) | (payload[9] << 24));
                        drone->pilotLatE7 = pLat;
                        drone->pilotLonE7 = pLon;
                    }
                }
                return;
            }
        }
        offset += 2 + elLen;
    }
}

void TacticalSuite::feedDroneBlePayload(const uint8_t* payload, size_t len, const uint8_t mac[6], int8_t rssi) {
    if (!payload || len < 4) return;
    // BLE Service Data UUID 0xFFFA (OpenDroneID)
    if (payload[0] == 0xFA && payload[1] == 0xFF) {
        DroneTarget* drone = findOrCreateDrone(mac);
        drone->rssi = rssi;
        drone->lastSeenMs = millis();
        // Parse message
        if (len >= 27) {
            const uint8_t* msg = payload + 2;
            uint8_t msgType = (msg[0] >> 4) & 0x0F;
            if (msgType == 0) {
                drone->uaType = msg[1] & 0x0F;
                char buf[21] = {0};
                memcpy(buf, msg + 2, 20);
                if (strlen(buf) > 3) strncpy(drone->uasId, buf, sizeof(drone->uasId) - 1);
            } else if (msgType == 1) {
                drone->hasLocation = true;
                drone->headingDeg = (uint16_t)msg[3] * 2;
                drone->speedMps = (float)msg[4] * 0.25f;
                int32_t lat = (int32_t)(msg[5] | (msg[6] << 8) | (msg[7] << 16) | (msg[8] << 24));
                int32_t lon = (int32_t)(msg[9] | (msg[10] << 8) | (msg[11] << 16) | (msg[12] << 24));
                drone->latitudeE7 = lat;
                drone->longitudeE7 = lon;
            }
        }
    }
}

void TacticalSuite::updateDroneSimLoop() {
    // Generate initial live tactical airspace targets if empty
    if (s_droneCount == 0) {
        uint8_t mac1[6] = {0x60, 0x60, 0x1F, 0x48, 0x22, 0x01};
        DroneTarget* d1 = findOrCreateDrone(mac1);
        strncpy(d1->uasId, "DJI-MAVIC3-4822", sizeof(d1->uasId) - 1);
        strncpy(d1->operatorId, "PILOT-US-9912", sizeof(d1->operatorId) - 1);
        d1->uaType = 2; // Multirotor
        d1->latitudeE7 = 37774929;
        d1->longitudeE7 = -122419416;
        d1->altitudeGeoM = 124.5f;
        d1->heightM = 82.0f;
        d1->speedMps = 14.2f;
        d1->headingDeg = 48;
        d1->pilotLatE7 = 37774500;
        d1->pilotLonE7 = -122419000;
        d1->rssi = -64;
        d1->channel = 6;
        d1->hasLocation = true;
        d1->hasPilot = true;
        d1->lastSeenMs = millis();

        uint8_t mac2[6] = {0x34, 0xD2, 0x70, 0x88, 0xBC, 0x14};
        DroneTarget* d2 = findOrCreateDrone(mac2);
        strncpy(d2->uasId, "AUTEL-EVO2-88BC", sizeof(d2->uasId) - 1);
        strncpy(d2->operatorId, "FAA-CERT-7734", sizeof(d2->operatorId) - 1);
        d2->uaType = 2;
        d2->latitudeE7 = 37776100;
        d2->longitudeE7 = -122421500;
        d2->altitudeGeoM = 65.0f;
        d2->heightM = 40.0f;
        d2->speedMps = 6.8f;
        d2->headingDeg = 210;
        d2->rssi = -79;
        d2->channel = 11;
        d2->hasLocation = true;
        d2->hasPilot = false;
        d2->lastSeenMs = millis();
    }

    // Dynamic flight simulation drift
    static uint32_t lastDrift = 0;
    if (millis() - lastDrift > 1000) {
        lastDrift = millis();
        for (uint8_t i = 0; i < s_droneCount; i++) {
            s_drones[i].headingDeg = (s_drones[i].headingDeg + 3) % 360;
            s_drones[i].lastSeenMs = millis();
            int8_t jitter = (int8_t)((millis() % 5) - 2);
            s_drones[i].rssi = (int8_t)fmaxf(-92.0f, fminf(-45.0f, (float)(s_drones[i].rssi + jitter)));
        }
    }
}

// ============================================================
// 2. SIGNAL FOX-HUNT DIRECTION SEEKER
// ============================================================
void TacticalSuite::setFoxTarget(const uint8_t mac[6], const char* name, FoxTargetType type, uint8_t ch) {
    memcpy(s_foxSession.targetMac, mac, 6);
    strncpy(s_foxSession.targetName, name ? name : "UNKNOWN TARGET", sizeof(s_foxSession.targetName) - 1);
    s_foxSession.targetType = type;
    s_foxSession.channel = ch;
    s_foxSession.currentRssi = -90;
    s_foxSession.filteredRssi = -90.0f;
    s_foxSession.peakRssi = -90;
    s_foxSession.lastSeenMs = millis();
    s_foxSession.isLocked = true;
    s_foxSession.estimatedDistanceM = 15.0f;
    s_foxSession.lastClickMs = millis();
}

void TacticalSuite::clearFoxTarget() {
    memset(s_foxSession.targetMac, 0, 6);
    strncpy(s_foxSession.targetName, "NO TARGET LOCKED", sizeof(s_foxSession.targetName) - 1);
    s_foxSession.isLocked = false;
    s_foxSession.currentRssi = -100;
    s_foxSession.filteredRssi = -100.0f;
    s_foxSession.peakRssi = -100;
}

const FoxHuntSession& TacticalSuite::getFoxSession() {
    return s_foxSession;
}

void TacticalSuite::updateFoxRssi(const uint8_t mac[6], int8_t rssi, uint8_t ch) {
    if (!s_foxSession.isLocked) return;
    if (memcmp(s_foxSession.targetMac, mac, 6) != 0) return;

    s_foxSession.currentRssi = rssi;
    s_foxSession.filteredRssi = (s_foxSession.filteredRssi * 0.70f) + ((float)rssi * 0.30f);
    if (rssi > s_foxSession.peakRssi) {
        s_foxSession.peakRssi = rssi;
    }
    if (ch > 0) s_foxSession.channel = ch;
    s_foxSession.lastSeenMs = millis();

    // Log-distance path loss estimation: d = 10 ^ ((TxPower - RSSI) / (10 * n))
    // TxPower ~ -45 dBm at 1 meter, path loss exponent n ~ 2.4
    float exp = (-45.0f - (float)rssi) / (10.0f * 2.4f);
    s_foxSession.estimatedDistanceM = fmaxf(0.2f, powf(10.0f, exp));
}

bool TacticalSuite::checkGeigerChirp(uint16_t& outFreq, uint16_t& outDur) {
    if (!s_foxSession.isLocked) return false;
    uint32_t now = millis();

    // Pulse interval based on proximity: -30dBm -> 50ms, -90dBm -> 800ms
    float factor = (-30.0f - s_foxSession.filteredRssi) / 60.0f; // 0.0 (closest) to 1.0 (furthest)
    factor = fmaxf(0.0f, fminf(1.0f, factor));
    uint32_t interval = (uint32_t)(50 + factor * 750);

    if (now - s_foxSession.lastClickMs >= interval) {
        s_foxSession.lastClickMs = now;
        // Pitch increases with proximity
        outFreq = (uint16_t)(3200 - factor * 1400); // 1800Hz to 3200Hz
        outDur = (uint16_t)(10 + (1.0f - factor) * 15);
        return true;
    }
    return false;
}

// ============================================================
// 3. COVERT SKIMMER & SURVEILLANCE SCANNER
// ============================================================
void TacticalSuite::startSurveillanceScan() {
    s_survScanning = true;
}

void TacticalSuite::stopSurveillanceScan() {
    s_survScanning = false;
}

uint8_t TacticalSuite::getSurveillanceCount() {
    return s_survCount;
}

bool TacticalSuite::getSurveillanceNode(uint8_t index, SurveillanceNode& outNode) {
    if (index >= s_survCount) return false;
    outNode = s_survNodes[index];
    return true;
}

static SurveillanceNode* registerSurveillanceHit(const uint8_t mac[6], const char* name, SurveillanceType type, ThreatLevel threat, int8_t rssi, uint8_t ch = 0) {
    for (uint8_t i = 0; i < s_survCount; i++) {
        if (memcmp(s_survNodes[i].mac, mac, 6) == 0) {
            s_survNodes[i].lastSeenMs = millis();
            s_survNodes[i].rssi = rssi;
            s_survNodes[i].packetCount++;
            return &s_survNodes[i];
        }
    }
    if (s_survCount < MAX_SURVEILLANCE_TARGETS) {
        SurveillanceNode* n = &s_survNodes[s_survCount++];
        memcpy(n->mac, mac, 6);
        strncpy(n->name, name ? name : "UNKNOWN", sizeof(n->name) - 1);
        n->type = type;
        n->threat = threat;
        n->rssi = rssi;
        n->channel = ch;
        n->firstSeenMs = millis();
        n->lastSeenMs = millis();
        n->packetCount = 1;
        return n;
    }
    return nullptr;
}

void TacticalSuite::checkBleDeviceForSurveillance(const uint8_t mac[6], const char* name, const uint8_t* mfgData, size_t mfgLen, int8_t rssi) {
    if (!name) return;
    // 1. Bluetooth ATM / Gas Pump Skimmers
    if (strstr(name, "HC-03") || strstr(name, "HC-05") || strstr(name, "HC-06") ||
        strstr(name, "BT-05") || strstr(name, "BT-06") || strstr(name, "SPP-CA")) {
        registerSurveillanceHit(mac, name, SURV_SKIMMER_HC05, THREAT_CRITICAL, rssi);
        return;
    }
    if (strstr(name, "RN-42") || strstr(name, "RN42")) {
        registerSurveillanceHit(mac, name, SURV_SKIMMER_RN42, THREAT_CRITICAL, rssi);
        return;
    }
    // 2. Flock Safety ALPR Cameras
    if (strstr(name, "FLOCK") || strstr(name, "FLK-") || strstr(name, "Flock")) {
        registerSurveillanceHit(mac, name, SURV_FLOCK_ALPR, THREAT_CRITICAL, rssi);
        return;
    }
    // 3. Apple AirTag / FindMy Stalker Detection
    if (mfgData && mfgLen >= 4 && mfgData[0] == 0x4C && mfgData[1] == 0x00 && mfgData[2] == 0x12) {
        char tagStr[32];
        snprintf(tagStr, sizeof(tagStr), "AIRTAG-%02X%02X%02X", mac[3], mac[4], mac[5]);
        registerSurveillanceHit(mac, tagStr, SURV_AIRTAG_STALKER, THREAT_ELEVATED, rssi);
        return;
    }
    // 4. Offensive Hardware / Flipper Zero
    if (strstr(name, "flipper") || strstr(name, "Flipper") || strstr(name, "fzp_")) {
        registerSurveillanceHit(mac, name, SURV_FLIPPER_ZERO, THREAT_ELEVATED, rssi);
        return;
    }
}

void TacticalSuite::checkWifiAPForSurveillance(const uint8_t bssid[6], const char* ssid, uint8_t channel, int8_t rssi) {
    if (!ssid) return;
    // Flock Safety Wi-Fi AP signature
    if (strstr(ssid, "FLOCK-") || strstr(ssid, "FlockSafety") || strstr(ssid, "FS_CAM")) {
        registerSurveillanceHit(bssid, ssid, SURV_FLOCK_ALPR, THREAT_CRITICAL, rssi, channel);
        return;
    }
    // Pwnagotchi Detection
    if (strstr(ssid, "pwnagotchi") || strstr(ssid, "PWN_")) {
        registerSurveillanceHit(bssid, ssid, SURV_PWNAGOTCHI, THREAT_ELEVATED, rssi, channel);
        return;
    }
}

void TacticalSuite::updateSurveillanceSimLoop() {
    if (s_survCount == 0) {
        uint8_t m1[6] = {0x00, 0x14, 0x03, 0x18, 0x90, 0x11};
        registerSurveillanceHit(m1, "HC-05 [PUMP #04 SKIMMER]", SURV_SKIMMER_HC05, THREAT_CRITICAL, -62);

        uint8_t m2[6] = {0x9C, 0x20, 0x7B, 0x51, 0x42, 0x88};
        registerSurveillanceHit(m2, "FLOCK-CAM-NORTH-08", SURV_FLOCK_ALPR, THREAT_CRITICAL, -71, 1);

        uint8_t m3[6] = {0x44, 0xA6, 0xE5, 0x21, 0x98, 0x33};
        registerSurveillanceHit(m3, "AIRTAG-NEARBY [219833]", SURV_AIRTAG_STALKER, THREAT_ELEVATED, -78);

        uint8_t m4[6] = {0xD0, 0x3F, 0x27, 0x44, 0x12, 0xFA};
        registerSurveillanceHit(m4, "flipper-pwn-unit", SURV_FLIPPER_ZERO, THREAT_INFO, -84);
    }
}
