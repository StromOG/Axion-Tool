#pragma once
#include <stdint.h>

enum SystemMode {
    MODE_BOOT = 0,
    MODE_MAIN_MENU,
    MODE_CAMERA,
    MODE_GALLERY,
    MODE_NOTEPAD,
    MODE_GAMES,
    MODE_REMOTE_ID,
    MODE_FOX_HUNT,
    MODE_SKIMMER_DETECTOR,
    MODE_WIFI_ANALYZER,
    MODE_BLE_ANALYZER,
    MODE_NRF_SPECTRUM,
    MODE_SUBGHZ,
    MODE_IR,
    MODE_SETTINGS,
    MODE_FILE_EXPLORER,
    MODE_ABOUT,
    MODE_HELP
};


enum InputEvent {
    EVT_NONE = 0,
    EVT_UP,
    EVT_DOWN,
    EVT_LEFT,
    EVT_RIGHT,
    EVT_CLICK,
    EVT_SHUTTER,
    EVT_SHUTTER_HOLD,
    EVT_MENU,
    EVT_BACK
};

struct SystemState {
    SystemMode mode;
    SystemMode previousMode;
    bool sdMounted;
    bool cameraActive;
    bool wifiActive;
    bool nrfActive;
    uint32_t uptimeMs;
    uint32_t framesRendered;
    float currentFps;
    float batteryVoltage;
    uint8_t batteryPercent;
};

extern SystemState g_state;
