#include "settings_manager.h"
#include "sd_manager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>

uint8_t SettingsManager::brightness = 100;
bool SettingsManager::soundEnabled = true;
uint8_t SettingsManager::channelHopDelay = 1;
uint8_t SettingsManager::cameraQuality = 10;
bool SettingsManager::ledEnabled = true;
char SettingsManager::imagePrefix[20] = "image";
uint32_t SettingsManager::imageIndex = 1;
bool SettingsManager::promptNaming = true;
uint8_t SettingsManager::imageFormat = 0; // 0 = JPG
char SettingsManager::operatorCallsign[24] = "OPERATOR_01";
uint8_t SettingsManager::startupMode = 0;      // 0 = Main Menu
bool SettingsManager::autoSavePcap = true;
uint16_t SettingsManager::foxHuntSensitivity = 65;
uint8_t SettingsManager::themePreset = 0; // THEME_CYBERPUNK
uint8_t SettingsManager::menuLayout = 0;  // LAYOUT_DASHBOARD
uint8_t SettingsManager::profileMode = 0; // PROFILE_STANDARD
CameraSettings SettingsManager::cam = {};

static const char* PRESET_PREFIXES[] = { "image", "photo", "shot", "capture", "doc", "axion" };
static const int PRESET_PREFIX_COUNT = sizeof(PRESET_PREFIXES) / sizeof(PRESET_PREFIXES[0]);

static void applyCameraDefaults(CameraSettings& c) {
    c.brightness    = 0;
    c.contrast      = 1;
    c.saturation    = 0;
    c.sharpness     = 2;
    c.aeLevel       = 0;
    c.denoise       = 4;
    c.gainCeiling   = 2;   // GAINCEILING_8X
    c.wbMode        = 0;   // Auto
    c.stillQuality  = 6;   // pristine
    c.stillResIdx   = 3;   // QXGA 3MP
    c.previewResIdx = 0;   // QVGA
    c.gridMode      = 1;   // Reticle
    c.scenePreset   = 0;   // Auto
    c.flipV         = true;
    c.mirrorH       = false;
}

void SettingsManager::resetDefaults() {
    brightness = 100;
    soundEnabled = true;
    channelHopDelay = 1;
    cameraQuality = 10;
    ledEnabled = true;
    strncpy(imagePrefix, "image", sizeof(imagePrefix));
    imageIndex = 1;
    promptNaming = true;
    imageFormat = 0;
    strncpy(operatorCallsign, "OPERATOR_01", sizeof(operatorCallsign));
    startupMode = 0;
    autoSavePcap = true;
    foxHuntSensitivity = 65;
    themePreset = 0;
    menuLayout = 0;
    profileMode = 0;
    applyCameraDefaults(cam);
}

void SettingsManager::init() {
    applyCameraDefaults(cam);
    load();
}

void SettingsManager::load() {
    if (!SDManager::isMounted()) return;

    // Check if INI config file exists first (human-friendly prioritized)
    if (SD_MMC.exists(PATH_CONFIG_INI)) {
        if (loadFromConfigFile(PATH_CONFIG_INI)) {
            Serial.println("[SETTINGS] Loaded configuration from " PATH_CONFIG_INI);
            return;
        }
    }

    if (!SD_MMC.exists(PATH_SETTINGS)) {
        // First boot with fresh SD: generate clean human-readable INI config
        generateDefaultConfigFile(PATH_CONFIG_INI);
        return;
    }

    File f = SD_MMC.open(PATH_SETTINGS, FILE_READ);
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[SETTINGS] Parse error: %s (using defaults)\n", err.c_str());
        return;
    }

    brightness      = doc["brightness"]      | 100;
    soundEnabled    = doc["soundEnabled"]    | true;
    channelHopDelay = doc["channelHopDelay"] | 1;
    cameraQuality   = doc["cameraQuality"]   | 10;
    ledEnabled      = doc["ledEnabled"]      | true;

    if (doc["imagePrefix"].is<const char*>()) {
        strncpy(imagePrefix, doc["imagePrefix"].as<const char*>(), sizeof(imagePrefix) - 1);
        imagePrefix[sizeof(imagePrefix) - 1] = '\0';
    }
    imageIndex   = doc["imageIndex"]   | 1;
    promptNaming = doc["promptNaming"] | true;
    imageFormat  = doc["imageFormat"]  | 0;

    JsonObject c = doc["camera"];
    if (!c.isNull()) {
        cam.brightness    = c["brightness"]    | cam.brightness;
        cam.contrast      = c["contrast"]      | cam.contrast;
        cam.saturation    = c["saturation"]    | cam.saturation;
        cam.sharpness     = c["sharpness"]     | cam.sharpness;
        cam.aeLevel       = c["aeLevel"]       | cam.aeLevel;
        cam.denoise       = c["denoise"]       | cam.denoise;
        cam.gainCeiling   = c["gainCeiling"]   | cam.gainCeiling;
        cam.wbMode        = c["wbMode"]        | cam.wbMode;
        cam.stillQuality  = c["stillQuality"]  | cam.stillQuality;
        cam.stillResIdx   = c["stillResIdx"]   | cam.stillResIdx;
        cam.previewResIdx = c["previewResIdx"] | cam.previewResIdx;
        cam.gridMode      = c["gridMode"]      | cam.gridMode;
        cam.scenePreset   = c["scenePreset"]   | cam.scenePreset;
        cam.flipV         = c["flipV"]         | cam.flipV;
        cam.mirrorH       = c["mirrorH"]       | cam.mirrorH;
    }
    Serial.println("[SETTINGS] Loaded configuration from SD");
}

void SettingsManager::save() {
    if (!SDManager::isMounted()) return;

    File f = SD_MMC.open(PATH_SETTINGS, FILE_WRITE);
    if (!f) return;

    JsonDocument doc;
    doc["version"]         = FW_VERSION;
    doc["brightness"]      = brightness;
    doc["soundEnabled"]    = soundEnabled;
    doc["channelHopDelay"] = channelHopDelay;
    doc["cameraQuality"]   = cameraQuality;
    doc["ledEnabled"]      = ledEnabled;
    doc["imagePrefix"]     = imagePrefix;
    doc["imageIndex"]      = imageIndex;
    doc["promptNaming"]    = promptNaming;
    doc["imageFormat"]     = imageFormat;

    JsonObject c = doc["camera"].to<JsonObject>();
    c["brightness"]    = cam.brightness;
    c["contrast"]      = cam.contrast;
    c["saturation"]    = cam.saturation;
    c["sharpness"]     = cam.sharpness;
    c["aeLevel"]       = cam.aeLevel;
    c["denoise"]       = cam.denoise;
    c["gainCeiling"]   = cam.gainCeiling;
    c["wbMode"]        = cam.wbMode;
    c["stillQuality"]  = cam.stillQuality;
    c["stillResIdx"]   = cam.stillResIdx;
    c["previewResIdx"] = cam.previewResIdx;
    c["gridMode"]      = cam.gridMode;
    c["scenePreset"]   = cam.scenePreset;
    c["flipV"]         = cam.flipV;
    c["mirrorH"]       = cam.mirrorH;

    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("[SETTINGS] Saved configuration to SD");

    // Also keep human-editable INI file synchronized
    saveToConfigFile(PATH_CONFIG_INI);
}

// ------------------------------------------------------------
// SMART INI CONFIGURATION FILE ENGINE
// ------------------------------------------------------------

static void trimString(char* s) {
    if (!s) return;
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    char* start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

void SettingsManager::generateDefaultConfigFile(const char* path) {
    if (!SDManager::isMounted()) return;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return;

    f.println("# ========================================================");
    f.println("# AXIONTOOL TACTICAL CYBER DECK - SMART CONFIGURATION FILE");
    f.println("# Firmware Target: ESP32-S3-WROOM-1 / N16R8 PSRAM");
    f.println("# Edit directly on SD Card or via USB Mass Storage.");
    f.println("# ========================================================\n");

    f.println("[SYSTEM]");
    f.printf("operator_callsign = %s\n", operatorCallsign);
    f.printf("startup_mode = %u        # 0:Main Menu, 1:Camera, 6:WiFi, 7:BLE, 8:SubGHz\n", (unsigned)startupMode);
    f.printf("brightness = %u          # 10..100 (Backlight intensity percentage)\n", (unsigned)brightness);
    f.printf("sound_enabled = %s       # true / false (Tactical audio feedback)\n", soundEnabled ? "true" : "false");
    f.printf("theme_preset = %u        # 0:Cyberpunk (Obsidian/Cyan), 1:Stealth (Carbon/White), 2:Field Amber\n", (unsigned)themePreset);
    f.printf("menu_layout = %u         # 0:Dashboard, 1:Grid 2x3, 2:Classic List\n", (unsigned)menuLayout);
    f.printf("profile_mode = %u        # 0:Standard, 1:Stealth Ops, 2:Sniffer Lab\n", (unsigned)profileMode);
    f.println();

    f.println("[RADIO_AND_TACTICAL]");
    f.printf("wifi_hop_delay_sec = %u  # 1..10 (Channel sweep dwell time)\n", (unsigned)channelHopDelay);
    f.printf("auto_save_pcap = %s      # Automatically save WPA2 handshakes to /HANDSHAKES/*.pcap\n", autoSavePcap ? "true" : "false");
    f.printf("foxhunt_sensitivity = %u # 20..95 (-dBm threshold for Geiger trigger)\n", (unsigned)foxHuntSensitivity);
    f.println();

    f.println("[STORAGE_AND_NAMING]");
    f.printf("image_prefix = %s        # Base prefix (image, photo, shot, capture, axion)\n", imagePrefix);
    f.printf("next_image_index = %u    # Next sequential file index\n", (unsigned)imageIndex);
    f.printf("prompt_naming = %s       # Prompt user with on-screen keyboard on capture\n", promptNaming ? "true" : "false");
    f.println();

    f.println("[CAMERA_ISP]");
    f.printf("still_resolution = %u    # 0:VGA, 1:HD, 2:UXGA(2MP), 3:QXGA(3MP)\n", (unsigned)cam.stillResIdx);
    f.printf("still_quality = %u       # 4..20 (JPEG quality, lower = sharper/larger)\n", (unsigned)cam.stillQuality);
    f.printf("brightness = %d          # -2..2\n", cam.brightness);
    f.printf("contrast = %d            # -2..2\n", cam.contrast);
    f.printf("saturation = %d          # -2..2\n", cam.saturation);
    f.printf("sharpness = %d           # -3..3\n", cam.sharpness);
    f.printf("denoise = %u             # 0..8\n", (unsigned)cam.denoise);
    f.printf("ae_level = %d            # -2..2\n", cam.aeLevel);
    f.printf("gain_ceiling = %u        # 0:2X, 1:4X, 2:8X, 3:16X, 4:32X, 5:64X, 6:128X\n", (unsigned)cam.gainCeiling);
    f.printf("white_balance = %u       # 0:Auto, 1:Sunny, 2:Cloudy, 3:Office, 4:Home\n", (unsigned)cam.wbMode);
    f.printf("grid_overlay = %u        # 0:None, 1:Reticle, 2:RuleOfThirds, 3:Horizon\n", (unsigned)cam.gridMode);
    f.printf("vertical_flip = %s       # true / false\n", cam.flipV ? "true" : "false");
    f.printf("horizontal_mirror = %s   # true / false\n", cam.mirrorH ? "true" : "false");

    f.close();
    Serial.printf("[SETTINGS] Generated clean smart config at %s\n", path);
}

bool SettingsManager::saveToConfigFile(const char* path) {
    if (!SDManager::isMounted()) return false;
    generateDefaultConfigFile(path);
    return true;
}

bool SettingsManager::loadFromConfigFile(const char* path) {
    if (!SDManager::isMounted()) return false;
    if (!SD_MMC.exists(path)) return false;

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return false;

    char line[128];
    char currentSection[32] = "";

    while (f.available()) {
        int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        trimString(line);

        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') continue;

        // Section header [NAME]
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            strncpy(currentSection, line + 1, sizeof(currentSection) - 1);
            currentSection[strlen(currentSection) - 1] = '\0';
            trimString(currentSection);
            continue;
        }

        // Key = Value
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trimString(key);

        // Strip inline comments in value
        char* comment = strpbrk(val, "#;");
        if (comment) *comment = '\0';
        trimString(val);

        auto toBool = [](const char* v) -> bool {
            return (strcasecmp(v, "true") == 0 || strcasecmp(v, "1") == 0 || strcasecmp(v, "yes") == 0 || strcasecmp(v, "on") == 0);
        };

        if (strcasecmp(currentSection, "SYSTEM") == 0) {
            if (strcasecmp(key, "operator_callsign") == 0) {
                strncpy(operatorCallsign, val, sizeof(operatorCallsign) - 1);
                operatorCallsign[sizeof(operatorCallsign) - 1] = '\0';
            } else if (strcasecmp(key, "startup_mode") == 0) startupMode = (uint8_t)atoi(val);
            else if (strcasecmp(key, "brightness") == 0) setBrightness((uint8_t)atoi(val));
            else if (strcasecmp(key, "sound_enabled") == 0) setSoundEnabled(toBool(val));
            else if (strcasecmp(key, "theme_preset") == 0) themePreset = (uint8_t)constrain(atoi(val), 0, THEME_PRESET_COUNT - 1);
            else if (strcasecmp(key, "menu_layout") == 0) menuLayout = (uint8_t)constrain(atoi(val), 0, LAYOUT_COUNT - 1);
            else if (strcasecmp(key, "profile_mode") == 0) setProfileMode((uint8_t)constrain(atoi(val), 0, PROFILE_COUNT - 1));
        } else if (strcasecmp(currentSection, "RADIO_AND_TACTICAL") == 0) {
            if (strcasecmp(key, "wifi_hop_delay_sec") == 0) channelHopDelay = (uint8_t)constrain(atoi(val), 1, 10);
            else if (strcasecmp(key, "auto_save_pcap") == 0) autoSavePcap = toBool(val);
            else if (strcasecmp(key, "foxhunt_sensitivity") == 0) foxHuntSensitivity = (uint16_t)constrain(atoi(val), 20, 95);
        } else if (strcasecmp(currentSection, "STORAGE_AND_NAMING") == 0) {
            if (strcasecmp(key, "image_prefix") == 0) setImagePrefix(val);
            else if (strcasecmp(key, "next_image_index") == 0) imageIndex = (uint32_t)strtoul(val, nullptr, 10);
            else if (strcasecmp(key, "prompt_naming") == 0) promptNaming = toBool(val);
        } else if (strcasecmp(currentSection, "CAMERA_ISP") == 0) {
            if (strcasecmp(key, "still_resolution") == 0) cam.stillResIdx = (uint8_t)constrain(atoi(val), 0, 3);
            else if (strcasecmp(key, "still_quality") == 0) cam.stillQuality = (uint8_t)constrain(atoi(val), 4, 20);
            else if (strcasecmp(key, "brightness") == 0) cam.brightness = (int8_t)constrain(atoi(val), -2, 2);
            else if (strcasecmp(key, "contrast") == 0) cam.contrast = (int8_t)constrain(atoi(val), -2, 2);
            else if (strcasecmp(key, "saturation") == 0) cam.saturation = (int8_t)constrain(atoi(val), -2, 2);
            else if (strcasecmp(key, "sharpness") == 0) cam.sharpness = (int8_t)constrain(atoi(val), -3, 3);
            else if (strcasecmp(key, "denoise") == 0) cam.denoise = (uint8_t)constrain(atoi(val), 0, 8);
            else if (strcasecmp(key, "ae_level") == 0) cam.aeLevel = (int8_t)constrain(atoi(val), -2, 2);
            else if (strcasecmp(key, "gain_ceiling") == 0) cam.gainCeiling = (uint8_t)constrain(atoi(val), 0, 6);
            else if (strcasecmp(key, "white_balance") == 0) cam.wbMode = (uint8_t)constrain(atoi(val), 0, 4);
            else if (strcasecmp(key, "grid_overlay") == 0) cam.gridMode = (uint8_t)constrain(atoi(val), 0, 3);
            else if (strcasecmp(key, "vertical_flip") == 0) cam.flipV = toBool(val);
            else if (strcasecmp(key, "horizontal_mirror") == 0) cam.mirrorH = toBool(val);
        }
    }
    f.close();
    return true;
}

uint8_t SettingsManager::getBrightness() { return brightness; }
void SettingsManager::setBrightness(uint8_t b) {
    if (b < 10) b = 10;
    if (b > 100) b = 100;
    brightness = b;
}

bool SettingsManager::isSoundEnabled() { return soundEnabled; }
void SettingsManager::setSoundEnabled(bool enabled) {
    soundEnabled = enabled;
    AudioManager::setEnabled(enabled);
}

uint8_t SettingsManager::getChannelHopDelay() { return channelHopDelay; }
void SettingsManager::setChannelHopDelay(uint8_t delaySec) { channelHopDelay = delaySec; }

uint8_t SettingsManager::getThemePreset() { return themePreset; }
void SettingsManager::setThemePreset(uint8_t theme) {
    if (theme < THEME_PRESET_COUNT) themePreset = theme;
}

uint8_t SettingsManager::getMenuLayout() { return menuLayout; }
void SettingsManager::setMenuLayout(uint8_t layout) {
    if (layout < LAYOUT_COUNT) menuLayout = layout;
}

uint8_t SettingsManager::getProfileMode() { return profileMode; }
void SettingsManager::setProfileMode(uint8_t prof) {
    if (prof < PROFILE_COUNT) {
        profileMode = prof;
        // Automated Profile Tuning
        if (prof == PROFILE_STEALTH_OPS) {
            setBrightness(25);
            setSoundEnabled(false);
            setLedEnabled(false);
        } else if (prof == PROFILE_SNIFFER_LAB) {
            setBrightness(85);
            setSoundEnabled(true);
            setChannelHopDelay(1);
            setAutoSavePcapEnabled(true);
        }
    }
}

const char* SettingsManager::getOperatorCallsign() { return operatorCallsign; }
void SettingsManager::setOperatorCallsign(const char* callsign) {
    if (callsign && strlen(callsign) > 0) {
        strncpy(operatorCallsign, callsign, sizeof(operatorCallsign) - 1);
        operatorCallsign[sizeof(operatorCallsign) - 1] = '\0';
    }
}

uint8_t SettingsManager::getStartupMode() { return startupMode; }
void SettingsManager::setStartupMode(uint8_t modeIdx) { startupMode = modeIdx; }

bool SettingsManager::isAutoSavePcapEnabled() { return autoSavePcap; }
void SettingsManager::setAutoSavePcapEnabled(bool enabled) { autoSavePcap = enabled; }

uint16_t SettingsManager::getFoxHuntSensitivity() { return foxHuntSensitivity; }
void SettingsManager::setFoxHuntSensitivity(uint16_t sens) { foxHuntSensitivity = sens; }

uint8_t SettingsManager::getCameraQuality() { return cameraQuality; }
void SettingsManager::setCameraQuality(uint8_t q) { cameraQuality = q; }

bool SettingsManager::isLedEnabled() { return ledEnabled; }
void SettingsManager::setLedEnabled(bool enabled) { ledEnabled = enabled; }

const char* SettingsManager::getImagePrefix() { return imagePrefix; }
void SettingsManager::setImagePrefix(const char* prefix) {
    if (prefix && strlen(prefix) > 0) {
        strncpy(imagePrefix, prefix, sizeof(imagePrefix) - 1);
        imagePrefix[sizeof(imagePrefix) - 1] = '\0';
    }
}

void SettingsManager::cycleImagePrefix() {
    int cur = -1;
    for (int i = 0; i < PRESET_PREFIX_COUNT; i++) {
        if (strcasecmp(imagePrefix, PRESET_PREFIXES[i]) == 0) { cur = i; break; }
    }
    cur = (cur + 1) % PRESET_PREFIX_COUNT;
    strncpy(imagePrefix, PRESET_PREFIXES[cur], sizeof(imagePrefix) - 1);
    imagePrefix[sizeof(imagePrefix) - 1] = '\0';
}

uint32_t SettingsManager::getNextImageIndex() { return imageIndex; }
void SettingsManager::incrementImageIndex() { imageIndex++; }

bool SettingsManager::isPromptNamingEnabled() { return promptNaming; }
void SettingsManager::setPromptNamingEnabled(bool enabled) { promptNaming = enabled; }

uint8_t SettingsManager::getImageFormat() { return 0; } // JPG locked
const char* SettingsManager::getImageFormatExtension() { return "JPG"; }

CameraSettings& SettingsManager::camera() { return cam; }
