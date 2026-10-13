#pragma once
#include <Arduino.h>

// Persisted camera / ISP configuration. Plain scalars so this module stays
// decoupled from esp_camera enums (CameraManager translates indices).
struct CameraSettings {
    int8_t  brightness;    // -2..2
    int8_t  contrast;      // -2..2
    int8_t  saturation;    // -2..2
    int8_t  sharpness;     // -3..3 (clamped by sensor)
    int8_t  aeLevel;       // -2..2
    uint8_t denoise;       // 0..8
    uint8_t gainCeiling;   // 0..6  -> GAINCEILING_2X..128X
    uint8_t wbMode;        // 0..4  (Auto/Sunny/Cloudy/Office/Home)
    uint8_t stillQuality;  // 4..20 (lower = better JPEG)
    uint8_t stillResIdx;   // 0..3  (VGA/HD/UXGA/QXGA)
    uint8_t previewResIdx; // 0..1  (QVGA/VGA)
    uint8_t gridMode;      // 0..3
    uint8_t scenePreset;   // 0..4  (Auto/Indoor/Outdoor/Night/Macro)
    bool    flipV;
    bool    mirrorH;
};

enum ThemePreset {
    THEME_CYBERPUNK = 0,
    THEME_AMOLED = 1,
    THEME_STEALTH = 2,
    THEME_FIELD_AMBER = 3,
    THEME_EMERALD = 4,
    THEME_SYNTHWAVE = 5,
    THEME_SOLARIZED = 6,
    THEME_PRESET_COUNT = 7
};

enum MenuLayout {
    LAYOUT_DASHBOARD = 0,
    LAYOUT_GRID = 1,
    LAYOUT_LIST = 2,
    LAYOUT_COUNT = 3
};

enum ProfileMode {
    PROFILE_STANDARD = 0,
    PROFILE_STEALTH_OPS = 1,
    PROFILE_SNIFFER_LAB = 2,
    PROFILE_COUNT = 3
};

class SettingsManager {
public:
    static void init();
    static void load();
    static void save();
    static void resetDefaults();

    // Smart INI Configuration File Engine
    static bool loadFromConfigFile(const char* path = "/axion_config.ini");
    static bool saveToConfigFile(const char* path = "/axion_config.ini");
    static void generateDefaultConfigFile(const char* path = "/axion_config.ini");

    // Display & Aesthetic Customization
    static uint8_t getBrightness();            // software dim level 10..100
    static void setBrightness(uint8_t b);

    static uint8_t getThemePreset();
    static void setThemePreset(uint8_t theme);

    static uint8_t getMenuLayout();
    static void setMenuLayout(uint8_t layout);

    static uint8_t getProfileMode();
    static void setProfileMode(uint8_t prof);

    static bool isSoundEnabled();
    static void setSoundEnabled(bool enabled);

    static uint8_t getChannelHopDelay();
    static void setChannelHopDelay(uint8_t delaySec);

    // Operator & Device Identity
    static const char* getOperatorCallsign();
    static void setOperatorCallsign(const char* callsign);

    static uint8_t getStartupMode();
    static void setStartupMode(uint8_t modeIdx);

    static bool isAutoSavePcapEnabled();
    static void setAutoSavePcapEnabled(bool enabled);

    static uint16_t getFoxHuntSensitivity();
    static void setFoxHuntSensitivity(uint16_t sens);

    // Legacy single-field accessor kept for compatibility
    static uint8_t getCameraQuality();
    static void setCameraQuality(uint8_t q);

    static bool isLedEnabled();
    static void setLedEnabled(bool enabled);

    // Image Naming & Storage Settings
    static const char* getImagePrefix();
    static void setImagePrefix(const char* prefix);
    static void cycleImagePrefix();
    static uint32_t getNextImageIndex();
    static void incrementImageIndex();
    static bool isPromptNamingEnabled();
    static void setPromptNamingEnabled(bool enabled);
    static uint8_t getImageFormat();            // 0 = JPG (fixed/locked)
    static const char* getImageFormatExtension();

    // Master Hardware Subsystem Controls (1-to-1 with physical BOM)
    static uint16_t getCpuSpeedMHz();
    static void setCpuSpeedMHz(uint16_t mhz);

    static uint8_t getFrameRateLimit();
    static void setFrameRateLimit(uint8_t fps);

    static uint8_t getNrfPower();
    static void setNrfPower(uint8_t pwr);

    static uint8_t getNrfDataRate();
    static void setNrfDataRate(uint8_t rate);

    static uint8_t getSubGhzFilter();
    static void setSubGhzFilter(uint8_t f);

    static bool getIrBoost();
    static void setIrBoost(bool b);

    static uint8_t getBuzzerMode();
    static void setBuzzerMode(uint8_t m);

    // Camera / ISP block (authoritative persisted copy)
    static CameraSettings& camera();           // mutable reference

private:
    static uint8_t brightness;
    static bool soundEnabled;
    static uint8_t channelHopDelay;
    static uint8_t cameraQuality;
    static bool ledEnabled;
    static char imagePrefix[20];
    static uint32_t imageIndex;
    static bool promptNaming;
    static uint8_t imageFormat;
    static char operatorCallsign[24];
    static uint8_t startupMode;
    static bool autoSavePcap;
    static uint16_t foxHuntSensitivity;
    static uint8_t themePreset;
    static uint8_t menuLayout;
    static uint8_t profileMode;
    static uint16_t cpuSpeedMHz;
    static uint8_t frameRateLimit;
    static uint8_t nrfPower;
    static uint8_t nrfDataRate;
    static uint8_t subGhzFilter;
    static bool irBoost;
    static uint8_t buzzerMode;
    static CameraSettings cam;
};
