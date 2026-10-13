#include <Arduino.h>
#include "pin_config.h"
#include "config.h"
#include "system_state.h"
#include "spi_arbiter.h"
#include "display_ui.h"
#include "input_controller.h"
#include "camera_manager.h"
#include "sd_manager.h"
#include "audio_manager.h"
#include "settings_manager.h"
#include "wifi_analyzer.h"
#include "ble_analyzer.h"
#include "nrf_scanner.h"
#include "subghz_engine.h"
#include "ir_engine.h"
#include "tactical_suite.h"
#include "icon_assets.h"
#include <SD_MMC.h>
#include <esp_heap_caps.h>

// Global System State
SystemState g_state = {
    .mode = MODE_BOOT,
    .previousMode = MODE_BOOT,
    .sdMounted = false,
    .cameraActive = false,
    .wifiActive = false,
    .nrfActive = false,
    .uptimeMs = 0,
    .framesRendered = 0,
    .currentFps = 0.0f,
    .batteryVoltage = 4.10f,
    .batteryPercent = 95
};

// Queue for Non-blocking Audio Chirps
struct AudioChirp { uint16_t freq; uint16_t duration; };
static QueueHandle_t audioQueue = nullptr;

// ---- Forward Declarations ----
static void switchMode(SystemMode newMode);
static void runCameraMode(InputEvent evt);
static void runGalleryMode(InputEvent evt);
static void runNotepadMode(InputEvent evt);
static void runGamesMode(InputEvent evt);
static void runMainMenu(InputEvent evt);
static void runWifiMode(InputEvent evt);
static void runBleMode(InputEvent evt);
static void runRemoteIdMode(InputEvent evt);
static void runFoxHuntMode(InputEvent evt);
static void runSkimmerMode(InputEvent evt);
static void runNrfMode(InputEvent evt);
static void runIrMode(InputEvent evt);
static void runSubGhzMode(InputEvent evt);
static void runSettingsMode(InputEvent evt);
static void runFileExplorerMode(InputEvent evt);
static void runAboutMode(InputEvent evt);
static void runHelpMode(InputEvent evt);

static int menuSelection = 0;
static int menuScroll = 0;

// Main Menu (Clear, direct technical labels)
static const MenuItem mainMenuItems[] = {
    { "Camera Viewfinder",     "Live 20+ FPS & 3MP Capture",   ICON_CAMERA,         THEME_CYAN },
    { "Photo & Video Gallery", "Review Photos & Video Clips",  ICON_GALLERY,        THEME_WHITE },
    { "SD File Explorer",      "Browse, View & Delete Files",  ICON_SD_CARD,        THEME_GREEN },
    { "Notepad Text Editor",   "Create & Edit SD TXT Notes",   ICON_DRAW,           THEME_CYAN },
    { "Cyber Mini-Games",      "Retro 4-in-1 Space Arcade",    ICON_GENERAL_APPS,   THEME_GREEN },
    { "2.4GHz RF Analyzer",    "126-Ch Spectrum & Waterfall",  ICON_PACKET_MONITOR, THEME_AMBER },
    { "Wi-Fi 802.11 Monitor",  "Beacon, Deauth & Encryption",  ICON_WIFI,           THEME_GREEN },
    { "BLE Proximity Scanner", "Discovery & Manufacturer IDs", ICON_BLUETOOTH,      THEME_CRIMSON },
    { "Drone Remote ID Sniff", "FAA ASTM F3411 Drone Radar",   ICON_PROBE_SNIFF,    THEME_AMBER },
    { "Signal Fox-Hunt Seeker","Proximity Direction Finding",  ICON_SCANNERS,       THEME_AMBER },
    { "Surveillance Scanner",  "Covert Skimmer & Cam Hunter",  ICON_CC_SKIMMERS,    THEME_CRIMSON },
    { "Sub-GHz 433MHz Engine", "Raw Pulse Capture & Replay",   ICON_SUBGHZ,         THEME_AMBER },
    { "Infrared Transceiver",  "TSOP Capture & Signal Blaster",ICON_IR,             THEME_CYAN },
    { "Device Feature Guide",  "Detailed Operation & Controls",ICON_DEVICE,         THEME_CYAN },
    { "System & Settings",     "Configuration & Diagnostics",  ICON_SETTINGS,       THEME_WHITE }
};
static const int MENU_COUNT = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);
static const int MENU_VISIBLE = 8;



// ============================================================
// CORE 0 (Radio / RF Transceiver Core)
// ============================================================
void taskCore0(void* pv) {
    while (true) {
        if (g_state.mode == MODE_WIFI_ANALYZER || g_state.mode == MODE_REMOTE_ID || g_state.mode == MODE_SKIMMER_DETECTOR) {
            WiFiAnalyzer::attackLoop();
        } else if (g_state.mode == MODE_BLE_ANALYZER) {
            BLEAnalyzer::spamLoop();
        } else if (g_state.mode == MODE_NRF_SPECTRUM) {
            NRFScanner::sweepStep();
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ============================================================
// CORE 1 TASK: Non-Blocking Audio Chirp Worker
// ============================================================
void taskAudio(void* pv) {
    AudioChirp chirp;
    while (true) {
        if (xQueueReceive(audioQueue, &chirp, portMAX_DELAY) == pdTRUE) {
            // GPIO 44 is shared with IR TX and the SubGHz RX line — never drive
            // the buzzer while those modes own the pin.
            if (SettingsManager::isSoundEnabled() &&
                g_state.mode != MODE_IR && g_state.mode != MODE_SUBGHZ) {
                AudioManager::beep(chirp.freq, chirp.duration);
            }
        }
    }
}

static void triggerChirp(uint16_t freq, uint16_t duration) {
    if (audioQueue) {
        AudioChirp c = { freq, duration };
        xQueueSend(audioQueue, &c, 0);
    }
}

// ============================================================
// CORE 1 (Application, UI, Viewfinder Core)
// ============================================================
void taskCore1(void* pv) {
    static uint32_t frameStart = 0;
    static uint32_t frameAccum = 0;
    static uint32_t frameCount = 0;
    static uint32_t lastBatteryCheckMs = 0;

    while (true) {
        frameStart = millis();
        g_state.uptimeMs = frameStart;

        if (millis() - lastBatteryCheckMs > 2000) {
            lastBatteryCheckMs = millis();
            g_state.batteryVoltage = InputController::getBatteryVoltage();
            g_state.batteryPercent = InputController::getBatteryPercent();
        }

        InputEvent evt = InputController::poll();

        switch (g_state.mode) {
            case MODE_MAIN_MENU:     runMainMenu(evt);        break;
            case MODE_CAMERA:        runCameraMode(evt);      break;
            case MODE_GALLERY:       runGalleryMode(evt);     break;
            case MODE_NOTEPAD:       runNotepadMode(evt);     break;
            case MODE_GAMES:         runGamesMode(evt);       break;
            case MODE_FILE_EXPLORER: runFileExplorerMode(evt);break;
            case MODE_NRF_SPECTRUM:  runNrfMode(evt);         break;
            case MODE_WIFI_ANALYZER: runWifiMode(evt);        break;
            case MODE_BLE_ANALYZER:  runBleMode(evt);         break;
            case MODE_REMOTE_ID:     runRemoteIdMode(evt);    break;
            case MODE_FOX_HUNT:      runFoxHuntMode(evt);     break;
            case MODE_SKIMMER_DETECTOR: runSkimmerMode(evt);  break;
            case MODE_SUBGHZ:        runSubGhzMode(evt);      break;
            case MODE_IR:            runIrMode(evt);          break;
            case MODE_HELP:          runHelpMode(evt);        break;
            case MODE_SETTINGS:      runSettingsMode(evt);    break;
            case MODE_ABOUT:         runAboutMode(evt);       break;
            default: break;
        }

        uint32_t now = millis();
        frameAccum += (now - frameStart);
        frameCount++;
        if (frameAccum >= 1000) {
            g_state.currentFps = (float)frameCount * 1000.0f / (float)frameAccum;
            frameAccum = 0; frameCount = 0;
        }

        uint32_t elapsed = millis() - frameStart;
        if (elapsed < INPUT_POLL_MS) vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS - elapsed));
    }
}

// ============================================================
// MAIN MENU (scrollable)
// ============================================================
static void drawMainMenu() {
    uint8_t layout = SettingsManager::getMenuLayout();

    DisplayUI::beginFrame();

    if (layout == LAYOUT_DASHBOARD) {
        // Read internal ESP32-S3 CPU temperature if available
        float tempC = 42.0f;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        tempC = temperatureRead();
#endif
        uint32_t freePsram = ESP.getFreePsram() / 1024;
        DisplayUI::drawDashboardHome(mainMenuItems, MENU_COUNT, menuSelection,
                                     g_state.batteryVoltage, g_state.batteryPercent,
                                     g_state.currentFps, g_state.sdMounted, tempC, freePsram);
    } else if (layout == LAYOUT_GRID) {
        DisplayUI::drawGridHome(mainMenuItems, MENU_COUNT, menuSelection);
    } else {
        // Classic Modern Vertical Stack
        if (menuSelection < menuScroll) menuScroll = menuSelection;
        if (menuSelection >= menuScroll + MENU_VISIBLE) menuScroll = menuSelection - MENU_VISIBLE + 1;
        int visible = MENU_COUNT - menuScroll;
        if (visible > MENU_VISIBLE) visible = MENU_VISIBLE;

        DisplayUI::drawAnimatedMenu(&mainMenuItems[menuScroll], visible, menuSelection - menuScroll, 0.0f);

        // Modern Pill Scrollbar Track & Thumb
        if (MENU_COUNT > MENU_VISIBLE) {
            auto* c = DisplayUI::canvas();
            int trackX = SCREEN_WIDTH - 4;
            int trackY = 26, trackH = 274;
            c->fillRoundRect(trackX - 1, trackY, 3, trackH, 1, THEME_PANEL);
            c->drawFastVLine(trackX, trackY, trackH, THEME_DIM_CYAN);
            int thumbH = trackH * MENU_VISIBLE / MENU_COUNT;
            int thumbY = trackY + (trackH - thumbH) * menuScroll / (MENU_COUNT - MENU_VISIBLE);
            c->fillRoundRect(trackX - 1, thumbY, 3, thumbH, 1, THEME_CYAN);
        }
    }

    char statBuf[52];
    snprintf(statBuf, sizeof(statBuf), "SD:%s | %.2fV %d%% | %.0f FPS",
             g_state.sdMounted ? "OK" : "NO", g_state.batteryVoltage, g_state.batteryPercent, g_state.currentFps);
    DisplayUI::drawBottomBar(statBuf);
    DisplayUI::endFrame();
}

static void runMainMenu(InputEvent evt) {
    static bool needsRedraw = true;
    static int lastSel = -1;
    uint8_t layout = SettingsManager::getMenuLayout();

    switch (evt) {
        case EVT_UP:
            if (layout == LAYOUT_GRID) {
                menuSelection = (menuSelection - 2 + MENU_COUNT) % MENU_COUNT;
            } else {
                menuSelection = (menuSelection - 1 + MENU_COUNT) % MENU_COUNT;
            }
            needsRedraw = true; triggerChirp(1400, 6); break;
        case EVT_DOWN:
            if (layout == LAYOUT_GRID) {
                menuSelection = (menuSelection + 2) % MENU_COUNT;
            } else {
                menuSelection = (menuSelection + 1) % MENU_COUNT;
            }
            needsRedraw = true; triggerChirp(1400, 6); break;
        case EVT_LEFT:
            if (layout == LAYOUT_GRID) {
                menuSelection = (menuSelection - 1 + MENU_COUNT) % MENU_COUNT;
                needsRedraw = true; triggerChirp(1400, 6);
            }
            break;
        case EVT_RIGHT:
            if (layout == LAYOUT_GRID) {
                menuSelection = (menuSelection + 1) % MENU_COUNT;
                needsRedraw = true; triggerChirp(1400, 6);
            }
            break;
        case EVT_CLICK:
        case EVT_SHUTTER:
            triggerChirp(1900, 10);
            switch (menuSelection) {
                case 0:  switchMode(MODE_CAMERA);           break;
                case 1:  switchMode(MODE_GALLERY);          break;
                case 2:  switchMode(MODE_FILE_EXPLORER);    break;
                case 3:  switchMode(MODE_NOTEPAD);          break;
                case 4:  switchMode(MODE_GAMES);            break;
                case 5:  switchMode(MODE_NRF_SPECTRUM);     break;
                case 6:  switchMode(MODE_WIFI_ANALYZER);    break;
                case 7:  switchMode(MODE_BLE_ANALYZER);     break;
                case 8:  switchMode(MODE_REMOTE_ID);        break;
                case 9:  switchMode(MODE_FOX_HUNT);         break;
                case 10: switchMode(MODE_SKIMMER_DETECTOR); break;
                case 11: switchMode(MODE_SUBGHZ);           break;
                case 12: switchMode(MODE_IR);               break;
                case 13: switchMode(MODE_HELP);             break;
                case 14: switchMode(MODE_SETTINGS);         break;
            }
            return;
        default: break;
    }


    if (needsRedraw || lastSel != menuSelection) {
        drawMainMenu();
        lastSel = menuSelection;
        needsRedraw = false;
    }
}

// ============================================================
// GALLERY state (shared with file explorer image view)
// ============================================================
#define MAX_MEDIA 128
static SdEntry mediaList[MAX_MEDIA];
static int mediaCount = 0;

static bool isMediaName(const char* n, bool& isVideo) {
    const char* dot = strrchr(n, '.');
    if (!dot) return false;
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) { isVideo = false; return true; }
    if (strcasecmp(dot, ".avi") == 0) { isVideo = true; return true; }
    return false;
}

static void refreshMedia() {
    mediaCount = 0;
    if (!SDManager::isMounted()) return;
    static SdEntry tmp[MAX_MEDIA];
    size_t n = SDManager::listDir(PATH_DCIM, tmp, MAX_MEDIA);
    for (size_t i = 0; i < n && mediaCount < MAX_MEDIA; i++) {
        bool v;
        if (!tmp[i].isDir && isMediaName(tmp[i].name, v)) {
            mediaList[mediaCount++] = tmp[i];
        }
    }
}

// ============================================================
// MODE SWITCHER
// ============================================================
static void switchMode(SystemMode newMode) {
    if (newMode == g_state.mode) return;
    Serial.printf("[SYS] Transition: %d -> %d\n", g_state.mode, newMode);

    switch (g_state.mode) {
        case MODE_CAMERA:
            CameraManager::storeToSettings();
            SettingsManager::save();
            CameraManager::deinit();
            g_state.cameraActive = false;
            break;
        case MODE_WIFI_ANALYZER:
            WiFiAnalyzer::stopAttack(); WiFiAnalyzer::stopPromiscuous();
            WiFiAnalyzer::stopPCAP();   WiFiAnalyzer::deinit();
            g_state.wifiActive = false; break;
        case MODE_BLE_ANALYZER:
            BLEAnalyzer::stopSpam(); BLEAnalyzer::stopScan(); BLEAnalyzer::deinit(); break;
        case MODE_REMOTE_ID:
            TacticalSuite::stopDroneScan();
            WiFiAnalyzer::stopPromiscuous();
            g_state.wifiActive = false;
            break;
        case MODE_FOX_HUNT:
            TacticalSuite::stopFoxHunt();
            break;
        case MODE_SKIMMER_DETECTOR:
            TacticalSuite::stopSurveillanceScan();
            WiFiAnalyzer::stopPromiscuous();
            g_state.wifiActive = false;
            break;
        case MODE_NRF_SPECTRUM:
            NRFScanner::stopSweep(); NRFScanner::deinit(); g_state.nrfActive = false; break;
        case MODE_IR:     IREngine::deinit();      break;
        case MODE_SUBGHZ: SubGhzEngine::deinit();  break;
        default: break;
    }

    switch (newMode) {
        case MODE_CAMERA:
            if (CameraManager::init()) g_state.cameraActive = true;
            break;
        case MODE_GALLERY:       refreshMedia();          break;
        case MODE_FILE_EXPLORER: /* handled on entry */   break;
        case MODE_WIFI_ANALYZER:
            if (WiFiAnalyzer::init()) { WiFiAnalyzer::startPromiscuous(); g_state.wifiActive = true; } break;
        case MODE_BLE_ANALYZER:
            if (BLEAnalyzer::init()) BLEAnalyzer::startScan(); break;
        case MODE_REMOTE_ID:
            TacticalSuite::startDroneScan();
            if (WiFiAnalyzer::init()) { WiFiAnalyzer::startPromiscuous(); g_state.wifiActive = true; } break;
        case MODE_FOX_HUNT:
            TacticalSuite::startFoxHunt("TARGET", nullptr, FOX_TYPE_WIFI_AP, 1); break;
        case MODE_SKIMMER_DETECTOR:
            TacticalSuite::startSurveillanceScan();
            if (WiFiAnalyzer::init()) { WiFiAnalyzer::startPromiscuous(); g_state.wifiActive = true; } break;
        case MODE_NRF_SPECTRUM:
            if (NRFScanner::init()) { NRFScanner::startSweep(); g_state.nrfActive = true; } break;
        case MODE_IR:     IREngine::init();     break;
        case MODE_SUBGHZ: SubGhzEngine::init(); break;
        default: break;
    }

    g_state.previousMode = g_state.mode;
    g_state.mode = newMode;
    DisplayUI::clear(THEME_BG);
}

// ============================================================
// 1. CAMERA MODE
// ============================================================
static int camQuickMenuSel = -1;

enum CamQuick {
    CQ_SCENE = 0, CQ_STILLRES, CQ_EFFECT, CQ_WB, CQ_BRIGHT, CQ_EV,
    CQ_GRID, CQ_ZOOM, CQ_FLIP, CQ_MIRROR, CQ_BURST, CQ_TIMELAPSE, CQ_CLOSE, CQ_COUNT
};

static void buildCamRows(char rows[][24]) {
    snprintf(rows[CQ_SCENE],   24, "Scene: %s",   CameraManager::getSceneName());
    snprintf(rows[CQ_STILLRES],24, "Res: %s",     CameraManager::getStillResolutionName());
    snprintf(rows[CQ_EFFECT],  24, "FX: %s",      CameraManager::getFilterName());
    snprintf(rows[CQ_WB],      24, "WB: %s",      CameraManager::getWbModeName());
    snprintf(rows[CQ_BRIGHT],  24, "Bright: %+d", CameraManager::getBrightness());
    snprintf(rows[CQ_EV],      24, "EV: %+d",     CameraManager::getExposure());
    snprintf(rows[CQ_GRID],    24, "Grid: %s",    CameraManager::getGridModeName());
    snprintf(rows[CQ_ZOOM],    24, "Zoom: %.1fx", CameraManager::getZoom());
    snprintf(rows[CQ_FLIP],    24, "Flip: %s",    CameraManager::getVFlip() ? "ON" : "OFF");
    snprintf(rows[CQ_MIRROR],  24, "Mirror: %s",  CameraManager::getHMirror() ? "ON" : "OFF");
    snprintf(rows[CQ_BURST],   24, "Burst Snap 3x");
    snprintf(rows[CQ_TIMELAPSE],24,"Timelapse: %s",CameraManager::isTimelapseRunning() ? "ON" : "OFF");
    snprintf(rows[CQ_CLOSE],   24, "Close Menu");
}

// State for on-screen photo naming keyboard
static bool s_namingActive = false;
static char s_namingBuffer[32] = {0};
static int s_kbdRow = 0;
static int s_kbdCol = 0;

static const char* s_kbdRows[4] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL_",
    "ZXCVBNM.-/"
};

static void runCameraMode(InputEvent evt) {
    if (!g_state.cameraActive) {
        DisplayUI::beginFrame();
        DisplayUI::drawCenteredText("CAMERA OFFLINE", SCREEN_HEIGHT / 2 - 10, THEME_CRIMSON, 2);
        DisplayUI::drawCenteredText("Check OV3660 ribbon cable", SCREEN_HEIGHT / 2 + 12, THEME_GREY, 1);
        DisplayUI::drawBottomBar("MENU:Home");
        DisplayUI::endFrame();
        if (evt == EVT_MENU || evt == EVT_BACK) switchMode(MODE_MAIN_MENU);
        return;
    }

    if (CameraManager::isTimelapseRunning() && CameraManager::tickTimelapse()) triggerChirp(2200, 10);

    // Interactive Photo Naming Dialog (Modal overlay)
    if (s_namingActive) {
        if (evt == EVT_MENU || evt == EVT_BACK) {
            // Cancel naming and discard
            s_namingActive = false;
            triggerChirp(1000, 15);
            return;
        }

        // Joystick navigation across 4-row 10-col matrix + top toolbar buttons
        if (evt == EVT_UP) {
            if (s_kbdRow == -1) {
                s_kbdRow = 3; // loop to bottom row
            } else if (s_kbdRow == 0) {
                s_kbdRow = -1; // up into top toolbar
                s_kbdCol = (s_kbdCol >= 5) ? 1 : 0; // 0=DEL, 1=SAVE
            } else {
                s_kbdRow--;
            }
            triggerChirp(1400, 5);
        } else if (evt == EVT_DOWN) {
            if (s_kbdRow == -1) {
                s_kbdRow = 0; // down into key matrix
                s_kbdCol = (s_kbdCol == 1) ? 8 : 4;
            } else if (s_kbdRow == 3) {
                s_kbdRow = -1; // loop to top toolbar
                s_kbdCol = (s_kbdCol >= 5) ? 1 : 0;
            } else {
                s_kbdRow++;
            }
            triggerChirp(1400, 5);
        } else if (evt == EVT_LEFT) {
            if (s_kbdRow == -1) {
                s_kbdCol = (s_kbdCol == 1) ? 0 : 1;
            } else {
                s_kbdCol = (s_kbdCol - 1 + 10) % 10;
            }
            triggerChirp(1400, 5);
        } else if (evt == EVT_RIGHT) {
            if (s_kbdRow == -1) {
                s_kbdCol = (s_kbdCol == 0) ? 1 : 0;
            } else {
                s_kbdCol = (s_kbdCol + 1) % 10;
            }
            triggerChirp(1400, 5);
        }

        // Click / Shutter action
        if (evt == EVT_CLICK) {
            if (s_kbdRow == -1 && s_kbdCol == 0) {
                // [DEL] backspace button in top toolbar
                int len = strlen(s_namingBuffer);
                if (len > 0) { s_namingBuffer[len - 1] = '\0'; triggerChirp(1200, 8); }
            } else if (s_kbdRow == -1 && s_kbdCol == 1) {
                // [SAVE] confirm filename and write still to SD
                if (strlen(s_namingBuffer) == 0) {
                    strncpy(s_namingBuffer, SettingsManager::getImagePrefix(), sizeof(s_namingBuffer) - 1);
                }
                char savedPath[80] = {0};
                triggerChirp(2400, 25);
                bool ok = CameraManager::captureHighResStill(savedPath, sizeof(savedPath), s_namingBuffer);
                s_namingActive = false;
                if (ok) {
                    triggerChirp(2800, 15);
                    DisplayUI::showCaptureFreezeFlash(savedPath);
                } else {
                    triggerChirp(800, 40);
                }
                return;
            } else if (s_kbdRow >= 0 && s_kbdRow < 4 && s_kbdCol >= 0 && s_kbdCol < 10) {
                // Type character
                int len = strlen(s_namingBuffer);
                if (len < 24) {
                    char ch = s_kbdRows[s_kbdRow][s_kbdCol];
                    s_namingBuffer[len] = ch;
                    s_namingBuffer[len + 1] = '\0';
                    triggerChirp(1900, 6);
                }
            }
        } else if (evt == EVT_SHUTTER) {
            // Quick Save with current buffer
            if (strlen(s_namingBuffer) == 0) {
                strncpy(s_namingBuffer, SettingsManager::getImagePrefix(), sizeof(s_namingBuffer) - 1);
            }
            char savedPath[80] = {0};
            triggerChirp(2400, 25);
            bool ok = CameraManager::captureHighResStill(savedPath, sizeof(savedPath), s_namingBuffer);
            s_namingActive = false;
            if (ok) {
                triggerChirp(2800, 15);
                DisplayUI::showCaptureFreezeFlash(savedPath);
            } else {
                triggerChirp(800, 40);
            }
            return;
        }


        // Render freeze frame / live preview under the dialog
        camera_fb_t* fb = CameraManager::capture();
        if (fb) {
            auto* d = DisplayUI::get();
            SPIScopedLock lock;
            d->startWrite();
            d->drawJpg(fb->buf, fb->len, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
            d->endWrite();
            CameraManager::release(fb);
        }

        DisplayUI::drawNamingKeyboardDialog(s_namingBuffer, s_kbdRow, s_kbdCol, SettingsManager::getImageFormatExtension(), "NAME PHOTO");
        return;
    }

    if (evt == EVT_MENU || evt == EVT_BACK) {
        if (camQuickMenuSel >= 0) { camQuickMenuSel = -1; triggerChirp(1200, 6); return; }
        if (CameraManager::isRecordingVideo()) CameraManager::stopVideoRecord();
        if (CameraManager::isTimelapseRunning()) CameraManager::stopTimelapse();
        switchMode(MODE_MAIN_MENU);
        return;
    }

    static char camRows[CQ_COUNT][24];
    static const char* camRowPtrs[CQ_COUNT];

    if (camQuickMenuSel >= 0) {
        if (evt == EVT_UP) { camQuickMenuSel = (camQuickMenuSel - 1 + CQ_COUNT) % CQ_COUNT; triggerChirp(1400, 6); }
        else if (evt == EVT_DOWN) { camQuickMenuSel = (camQuickMenuSel + 1) % CQ_COUNT; triggerChirp(1400, 6); }
        else if (evt == EVT_LEFT || evt == EVT_RIGHT || evt == EVT_CLICK) {
            int dir = (evt == EVT_LEFT) ? -1 : 1;
            triggerChirp(1700, 8);
            switch (camQuickMenuSel) {
                case CQ_SCENE:    CameraManager::cycleScene(); break;
                case CQ_STILLRES: CameraManager::cycleStillResolution(); break;
                case CQ_EFFECT:
                    if (dir < 0) CameraManager::setFilter((CameraFilterMode)(((int)CameraManager::getFilter() - 1 + FILTER_COUNT) % FILTER_COUNT));
                    else CameraManager::cycleFilter();
                    break;
                case CQ_WB:     CameraManager::cycleWbMode(); break;
                case CQ_BRIGHT: CameraManager::adjustBrightness(dir); break;
                case CQ_EV:     CameraManager::adjustExposure(dir); break;
                case CQ_GRID:   CameraManager::cycleGridMode(); break;
                case CQ_ZOOM:   CameraManager::adjustZoom(dir * 0.5f); break;
                case CQ_FLIP:   CameraManager::toggleVFlip(); break;
                case CQ_MIRROR: CameraManager::toggleHMirror(); break;
                case CQ_BURST:
                    camQuickMenuSel = -1; triggerChirp(2600, 15);
                    CameraManager::captureBurst(3); triggerChirp(3000, 30); return;
                case CQ_TIMELAPSE:
                    if (CameraManager::isTimelapseRunning()) { CameraManager::stopTimelapse(); triggerChirp(1000, 20); }
                    else { CameraManager::startTimelapse(3); triggerChirp(2500, 20); }
                    camQuickMenuSel = -1; return;
                case CQ_CLOSE:  camQuickMenuSel = -1; return;
            }
        }
    } else {
        if (evt == EVT_CLICK) { camQuickMenuSel = 0; triggerChirp(1600, 10); return; }
        if (evt == EVT_LEFT)       { CameraManager::cycleFilter(); triggerChirp(1200, 6); }
        else if (evt == EVT_RIGHT) { CameraManager::cycleScene();  triggerChirp(1600, 6); }
        else if (evt == EVT_UP)    { CameraManager::adjustExposure(1);  triggerChirp(1400, 6); }
        else if (evt == EVT_DOWN)  { CameraManager::adjustExposure(-1); triggerChirp(1000, 6); }

        if (evt == EVT_SHUTTER_HOLD) {
            if (CameraManager::isRecordingVideo()) {
                CameraManager::stopVideoRecord();
                triggerChirp(1000, 30);
                DisplayUI::showVideoRecordAnimation(false);
            } else {
                CameraManager::startVideoRecord();
                triggerChirp(2000, 40);
                DisplayUI::showVideoRecordAnimation(true);
            }
            return;
        }
        if (evt == EVT_SHUTTER) {
            if (CameraManager::isRecordingVideo()) {
                CameraManager::stopVideoRecord();
                triggerChirp(1000, 30);
                DisplayUI::showVideoRecordAnimation(false);
            } else {
                if (SettingsManager::isPromptNamingEnabled()) {
                    // Open naming keyboard modal preloaded with sequential prefix
                    s_namingActive = true;
                    uint32_t idx = SettingsManager::getNextImageIndex();
                    const char* pfx = SettingsManager::getImagePrefix();
                    if (idx == 1) snprintf(s_namingBuffer, sizeof(s_namingBuffer), "%s", pfx);
                    else snprintf(s_namingBuffer, sizeof(s_namingBuffer), "%s%u", pfx, (unsigned)idx);
                    s_kbdRow = -1; s_kbdCol = 1; // Focus on top-right [SAVE] button initially
                    triggerChirp(2000, 15);
                    return;
                } else {
                    // Direct sequential capture without modal prompt
                    char savedPath[80] = {0};
                    triggerChirp(2400, 25);
                    bool ok = CameraManager::captureHighResStill(savedPath, sizeof(savedPath));
                    if (ok) { triggerChirp(2800, 15); DisplayUI::showCaptureFreezeFlash(savedPath); }
                    else triggerChirp(800, 40);
                }
            }
            return;
        }
    }

    // Live frame acquisition + direct blit (unbuffered for speed)
    camera_fb_t* fb = CameraManager::capture();
    if (!fb) return;
    if (CameraManager::isRecordingVideo()) CameraManager::appendVideoFrame(fb);

    auto* d = DisplayUI::get();
    {
        SPIScopedLock lock;
        d->startWrite();
        // Center the 4:3 sensor frame on the portrait panel (letterboxed, HUD on margins)
        d->drawJpg(fb->buf, fb->len, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
        d->endWrite();
    }
    CameraManager::release(fb);
    g_state.framesRendered++;

    DisplayUI::drawCameraViewfinderHUD(
        g_state.currentFps, SDManager::getFreeSpaceMB(), g_state.sdMounted,
        CameraManager::getZoom(), CameraManager::getStillResolutionName(), CameraManager::getFilterName(),
        CameraManager::getBrightness(), CameraManager::getContrast(), CameraManager::getGridMode(),
        CameraManager::isRecordingVideo(), CameraManager::isTimelapseRunning(),
        CameraManager::getWbModeName(), camQuickMenuSel,
        CameraManager::getVideoElapsedSec(), CameraManager::getVideoFramesRecorded(),
        CameraManager::getSceneName());

    if (camQuickMenuSel >= 0) {
        buildCamRows(camRows);
        for (int i = 0; i < CQ_COUNT; i++) camRowPtrs[i] = camRows[i];
        DisplayUI::drawCameraQuickMenu(camQuickMenuSel, camRowPtrs, CQ_COUNT);
    }
}

// ============================================================
// 2. GALLERY (photos + video playback)
// ============================================================
static int galleryIndex = 0;
static bool videoOpen = false;
static File videoFileRd;
static uint8_t* videoBuf = nullptr;
static const size_t VIDEO_BUF_SZ = 100 * 1024;
static int videoFrameIdx = 0;
static int videoTotalFrames = 0;
static bool videoPlaying = false;
static size_t videoLastLen = 0;

static void closeVideo() {
    if (videoFileRd) videoFileRd.close();
    if (videoBuf) { free(videoBuf); videoBuf = nullptr; }
    videoOpen = false; videoPlaying = false; videoFrameIdx = 0; videoLastLen = 0;
}

static bool openVideo(const char* path) {
    closeVideo();
    videoFileRd = SD_MMC.open(path, FILE_READ);
    if (!videoFileRd) return false;
    videoBuf = (uint8_t*)ps_malloc(VIDEO_BUF_SZ);
    if (!videoBuf) videoBuf = (uint8_t*)malloc(VIDEO_BUF_SZ);
    if (!videoBuf) { videoFileRd.close(); return false; }
    videoFileRd.seek(220);  // skip AVI header
    videoOpen = true; videoPlaying = true; videoFrameIdx = 0;
    // rough frame count from avih (offset 48)
    videoTotalFrames = 0;
    uint32_t pos = videoFileRd.position();
    videoFileRd.seek(48);
    uint8_t fb4[4]; videoFileRd.read(fb4, 4);
    videoTotalFrames = fb4[0] | (fb4[1] << 8) | (fb4[2] << 16) | (fb4[3] << 24);
    videoFileRd.seek(pos);
    return true;
}

// Reads the next MJPEG chunk into videoBuf; returns length or 0 at EOF
static size_t readNextVideoFrame() {
    if (!videoFileRd) return 0;
    uint8_t hdr[8];
    while (videoFileRd.available() >= 8) {
        if (videoFileRd.read(hdr, 8) != 8) return 0;
        uint32_t len = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
        if (hdr[0] == '0' && hdr[1] == '0' && (hdr[2] == 'd' || hdr[2] == 'D')) {
            if (len == 0 || len > VIDEO_BUF_SZ) { videoFileRd.seek(videoFileRd.position() + len + (len & 1)); continue; }
            size_t got = videoFileRd.read(videoBuf, len);
            if (len & 1) videoFileRd.read(hdr, 1); // padding byte
            return got;
        } else {
            // unknown chunk, skip
            videoFileRd.seek(videoFileRd.position() + len + (len & 1));
        }
    }
    return 0;
}

static void runGalleryMode(InputEvent evt) {
    static bool needsRedraw = true;

    if (videoOpen) {
        if (evt == EVT_MENU || evt == EVT_BACK) { closeVideo(); needsRedraw = true; return; }
        if (evt == EVT_CLICK) { videoPlaying = !videoPlaying; triggerChirp(1600, 8); }
        if (evt == EVT_LEFT)  { videoFileRd.seek(220); videoFrameIdx = 0; triggerChirp(1200, 6); }
        if (evt == EVT_RIGHT) { for (int i = 0; i < 10; i++) { if (readNextVideoFrame()) videoFrameIdx++; } triggerChirp(1200, 6); }

        if (videoPlaying) {
            size_t len = readNextVideoFrame();
            if (len == 0) { videoFileRd.seek(220); videoFrameIdx = 0; len = readNextVideoFrame(); }
            if (len > 0) {
                videoFrameIdx++;
                videoLastLen = len;
                DisplayUI::beginFrame();
                DisplayUI::drawVideoFrame(videoBuf, len, videoFrameIdx, videoTotalFrames, true);
                DisplayUI::endFrame();
            }
        } else {
            // paused: redraw the last decoded frame
            DisplayUI::beginFrame();
            DisplayUI::drawVideoFrame(videoBuf, videoLastLen, videoFrameIdx, videoTotalFrames, false);
            DisplayUI::endFrame();
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        return;
    }

    if (evt == EVT_MENU || evt == EVT_BACK) { switchMode(MODE_MAIN_MENU); return; }

    if (mediaCount == 0) {
        if (needsRedraw) {
            DisplayUI::beginFrame();
            DisplayUI::drawGalleryScreen("", 0, 0, false, false);
            DisplayUI::endFrame();
            needsRedraw = false;
        }
        return;
    }

    if (galleryIndex >= mediaCount) galleryIndex = mediaCount - 1;
    if (galleryIndex < 0) galleryIndex = 0;

    bool isVideo = false; isMediaName(mediaList[galleryIndex].name, isVideo);
    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", PATH_DCIM, mediaList[galleryIndex].name);

    if (evt == EVT_LEFT)  { galleryIndex = (galleryIndex - 1 + mediaCount) % mediaCount; needsRedraw = true; triggerChirp(1400, 6); }
    else if (evt == EVT_RIGHT) { galleryIndex = (galleryIndex + 1) % mediaCount; needsRedraw = true; triggerChirp(1400, 6); }
    else if (evt == EVT_CLICK) {
        if (isVideo) { if (openVideo(fullPath)) { triggerChirp(2000, 12); } return; }
        // photo: delete
        if (SD_MMC.remove(fullPath)) {
            triggerChirp(800, 30);
            refreshMedia();
            if (galleryIndex >= mediaCount) galleryIndex = mediaCount - 1;
            needsRedraw = true;
        }
    }

    if (needsRedraw) {
        bool valid = SD_MMC.exists(fullPath);
        DisplayUI::beginFrame();
        DisplayUI::drawGalleryScreen(fullPath, galleryIndex, mediaCount, valid, isVideo);
        DisplayUI::endFrame();
        needsRedraw = false;
    }
}

// ============================================================
// NOTEPAD TEXT EDITOR STATE & LOGIC (REVAMPED CYBER EDITOR)
// ============================================================
static char noteBuffer[512] = "";
static char noteFileName[32] = "note_001";
static bool noteNamingStep = false;
static int noteKbdRow = 0;
static int noteKbdCol = 0;
static uint32_t noteToastUntil = 0;
static int s_noteFileCounter = 1;
static int s_notepadLastMode = -1;
static bool noteExitPrompt = false;
static int noteExitChoice = 0; // 0 = SAVE & EXIT, 1 = DISCARD
static bool noteIsDirty = false;

static const char* s_noteKbdMatrix[4] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL-",
    "ZXCVBNM.,/"
};

static void initNewCleanNote() {
    noteBuffer[0] = '\0';
    snprintf(noteFileName, sizeof(noteFileName), "note_%03d", s_noteFileCounter);
    noteNamingStep = false;
    noteKbdRow = 0;
    noteKbdCol = 0;
    noteToastUntil = 0;
    noteIsDirty = false;
    noteExitPrompt = false;
    noteExitChoice = 0;
}

static void saveNoteToSD() {
    if (strlen(noteFileName) == 0) {
        snprintf(noteFileName, sizeof(noteFileName), "note_%03d", s_noteFileCounter);
    }
    char path[64];
    snprintf(path, sizeof(path), "/NOTEPAD/%s.TXT", noteFileName);
    if (g_state.sdMounted) {
        if (!SD_MMC.exists("/NOTEPAD")) SD_MMC.mkdir("/NOTEPAD");
        File f = SD_MMC.open(path, FILE_WRITE);
        if (f) {
            f.write((const uint8_t*)noteBuffer, strlen(noteBuffer));
            f.close();
        }
    }
    s_noteFileCounter++;
    noteToastUntil = millis() + 2500;
    noteIsDirty = false;
    triggerChirp(2600, 25);
}

static void runNotepadMode(InputEvent evt) {
    // ALWAYS start with a 100% clean, empty new file on entry
    if (s_notepadLastMode != MODE_NOTEPAD) {
        initNewCleanNote();
        s_notepadLastMode = MODE_NOTEPAD;
    }

    // Modal Exit Confirmation Handling (SAVE vs DISCARD)
    if (noteExitPrompt) {
        if (evt == EVT_MENU || evt == EVT_BACK) {
            noteExitPrompt = false;
            triggerChirp(1200, 8);
            return;
        }
        if (evt == EVT_LEFT) {
            noteExitChoice = 0;
            triggerChirp(1400, 4);
        } else if (evt == EVT_RIGHT) {
            noteExitChoice = 1;
            triggerChirp(1400, 4);
        } else if (evt == EVT_UP || evt == EVT_DOWN) {
            noteExitChoice = 1 - noteExitChoice;
            triggerChirp(1400, 4);
        } else if (evt == EVT_CLICK || evt == EVT_SHUTTER) {
            if (noteExitChoice == 0) {
                // SAVE & EXIT: commit note to SD card, clear buffer, and return to main menu
                saveNoteToSD();
                noteBuffer[0] = '\0';
                noteIsDirty = false;
                noteExitPrompt = false;
                s_notepadLastMode = -1;
                switchMode(MODE_MAIN_MENU);
                return;
            } else {
                // DISCARD & EXIT: discard all changes, clear buffer, warn audio, and return to main menu
                triggerChirp(800, 30);
                noteBuffer[0] = '\0';
                noteIsDirty = false;
                noteExitPrompt = false;
                s_notepadLastMode = -1;
                switchMode(MODE_MAIN_MENU);
                return;
            }
        }

        DisplayUI::beginFrame();
        char fnDisp[40];
        snprintf(fnDisp, sizeof(fnDisp), "%s.TXT", noteFileName);
        DisplayUI::drawNotepadEditor(fnDisp, noteBuffer, noteKbdRow, noteKbdCol, noteNamingStep, noteFileName, noteToastUntil, noteExitPrompt, noteExitChoice);
        DisplayUI::endFrame();
        return;
    }

    if (evt == EVT_MENU || evt == EVT_BACK) {
        if (noteNamingStep) {
            noteNamingStep = false;
            noteKbdRow = -1; noteKbdCol = 4;
            triggerChirp(1000, 10);
            return;
        }
        // Check if anything is written and note has unsaved changes
        if (noteIsDirty && strlen(noteBuffer) > 0) {
            noteExitPrompt = true;
            noteExitChoice = 0; // Default to SAVE & EXIT for data safety
            triggerChirp(2000, 15);
            return;
        }
        s_notepadLastMode = -1; // Ready for next fresh entry
        switchMode(MODE_MAIN_MENU);
        return;
    }

    if (evt == EVT_UP) {
        if (noteKbdRow == -1) {
            noteKbdRow = 3;
            noteKbdCol = (noteKbdCol * 2 < 10) ? noteKbdCol * 2 : 9;
        } else if (noteKbdRow == 0) {
            noteKbdRow = -1;
            noteKbdCol = (noteKbdCol < 2) ? 0 : (noteKbdCol < 4 ? 1 : (noteKbdCol < 6 ? 2 : (noteKbdCol < 8 ? 3 : 4)));
        } else {
            noteKbdRow--;
        }
        triggerChirp(1400, 4);
    } else if (evt == EVT_DOWN) {
        if (noteKbdRow == -1) {
            noteKbdRow = 0;
            noteKbdCol = (noteKbdCol * 2 < 10) ? noteKbdCol * 2 : 9;
        } else if (noteKbdRow == 3) {
            noteKbdRow = -1;
            noteKbdCol = (noteKbdCol < 2) ? 0 : (noteKbdCol < 4 ? 1 : (noteKbdCol < 6 ? 2 : (noteKbdCol < 8 ? 3 : 4)));
        } else {
            noteKbdRow++;
        }
        triggerChirp(1400, 4);
    } else if (evt == EVT_LEFT) {
        if (noteKbdRow == -1) noteKbdCol = (noteKbdCol - 1 + 5) % 5;
        else noteKbdCol = (noteKbdCol - 1 + 10) % 10;
        triggerChirp(1400, 4);
    } else if (evt == EVT_RIGHT) {
        if (noteKbdRow == -1) noteKbdCol = (noteKbdCol + 1) % 5;
        else noteKbdCol = (noteKbdCol + 1) % 10;
        triggerChirp(1400, 4);
    }

    if (evt == EVT_CLICK) {
        if (noteKbdRow == -1) {
            // Action Bar: [DEL], [SPACE], [ENTER], [NEW], [SAVE]
            if (noteKbdCol == 0) {
                // DEL
                int len = strlen(noteBuffer);
                if (len > 0) {
                    noteBuffer[len - 1] = '\0';
                    noteIsDirty = true;
                    triggerChirp(1200, 8);
                }
            } else if (noteKbdCol == 1) {
                // SPACE
                int len = strlen(noteBuffer);
                if (len < 480) {
                    noteBuffer[len] = ' ';
                    noteBuffer[len + 1] = '\0';
                    noteIsDirty = true;
                    triggerChirp(1700, 6);
                }
            } else if (noteKbdCol == 2) {
                // ENTER (New line)
                int len = strlen(noteBuffer);
                if (len < 480) {
                    noteBuffer[len] = '\n';
                    noteBuffer[len + 1] = '\0';
                    noteIsDirty = true;
                    triggerChirp(1500, 8);
                }
            } else if (noteKbdCol == 3) {
                // NEW (Wipe and start fresh clean file)
                s_noteFileCounter++;
                initNewCleanNote();
                triggerChirp(1000, 20);
            } else if (noteKbdCol == 4) {
                // SAVE
                saveNoteToSD();
            }
        } else if (noteKbdRow >= 0 && noteKbdRow < 4 && noteKbdCol >= 0 && noteKbdCol < 10) {
            char ch = s_noteKbdMatrix[noteKbdRow][noteKbdCol];
            int len = strlen(noteBuffer);
            if (len < 480) {
                noteBuffer[len] = ch;
                noteBuffer[len + 1] = '\0';
                noteIsDirty = true;
                triggerChirp(1900, 6);
            }
        }
    } else if (evt == EVT_SHUTTER) {
        // Fast instant save shortcut
        saveNoteToSD();
    }

    DisplayUI::beginFrame();
    char fnDisp[40];
    snprintf(fnDisp, sizeof(fnDisp), "%s.TXT", noteFileName);
    DisplayUI::drawNotepadEditor(fnDisp, noteBuffer, noteKbdRow, noteKbdCol, noteNamingStep, noteFileName, noteToastUntil, noteExitPrompt, noteExitChoice);
    DisplayUI::endFrame();
}

// ============================================================
// CYBER RETRO MINI-GAMES ARCADE LOGIC (SPACE ARCADE)
// ============================================================
static int g_curGame = 0;      // 0=Menu, 1=Snake, 2=Pong, 3=Flappy, 4=Breakout
static int g_gameSubState = 0; // 0=Running, 1=Pause, 2=Game Over
static int g_gameScore = 0;
static int g_gameHighScore[5] = {0, 0, 0, 0, 0};
static uint32_t g_lastGameTick = 0;
static int g_spaceSeed = 42;

// Game 1: Snake State
#define MAX_SNAKE 128
static uint16_t snakeBody[MAX_SNAKE];
static int snakeLen = 4;
static int snakeDir = 0; // 0=Right, 1=Down, 2=Left, 3=Up
static int snakeFoodX = 14, snakeFoodY = 12;

// Game 2: Neon Pong State
static int pongPlayerY = 140, pongAiY = 140;
static int pongBallX = 120, pongBallY = 150;
static int pongBallVx = 3, pongBallVy = 2;

// Game 3: Cyber Flappy State
static int flappyBirdY = 140;
static float flappyVy = 0.0f;
static int flappyPipeX = 220;
static int flappyPipeGapY = 90;

// Game 4: Neon Breakout State
static int breakoutPadX = 100;
static int breakoutBallX = 120, breakoutBallY = 180;
static int breakoutBallVx = 3, breakoutBallVy = -3;
static uint8_t breakoutBricks[24];

static void initSnake() {
    snakeLen = 4;
    snakeDir = 0;
    snakeBody[0] = 10 | (12 << 8);
    snakeBody[1] = 9 | (12 << 8);
    snakeBody[2] = 8 | (12 << 8);
    snakeBody[3] = 7 | (12 << 8);
    snakeFoodX = 18; snakeFoodY = 12;
    g_gameScore = 0;
    g_gameSubState = 0;
}

static void initPong() {
    pongPlayerY = 140; pongAiY = 140;
    pongBallX = 120; pongBallY = 150;
    pongBallVx = 3; pongBallVy = 2;
    g_gameScore = 0;
    g_gameSubState = 0;
}

static void initFlappy() {
    flappyBirdY = 140;
    flappyVy = 0.0f;
    flappyPipeX = 220;
    flappyPipeGapY = 90;
    g_gameScore = 0;
    g_gameSubState = 0;
}

static void initBreakout() {
    breakoutPadX = 100;
    breakoutBallX = 120;
    breakoutBallY = 180;
    breakoutBallVx = 3;
    breakoutBallVy = -3;
    for (int i = 0; i < 24; i++) breakoutBricks[i] = 1;
    g_gameScore = 0;
    g_gameSubState = 0;
}

static void runGamesMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK) {
        if (g_curGame > 0) {
            g_curGame = 0;
            g_gameSubState = 0;
            triggerChirp(1200, 10);
            return;
        }
        switchMode(MODE_MAIN_MENU);
        return;
    }

    uint32_t now = millis();

    if (g_curGame == 0) {
        // Game Selection Menu (4 arcade games)
        static int selGame = 0;
        if (evt == EVT_UP) { selGame = (selGame - 1 + 4) % 4; triggerChirp(1400, 5); }
        else if (evt == EVT_DOWN) { selGame = (selGame + 1) % 4; triggerChirp(1400, 5); }
        else if (evt == EVT_CLICK || evt == EVT_SHUTTER) {
            g_curGame = selGame + 1;
            g_spaceSeed = (int)millis();
            triggerChirp(2200, 15);
            if (g_curGame == 1) initSnake();
            else if (g_curGame == 2) initPong();
            else if (g_curGame == 3) initFlappy();
            else if (g_curGame == 4) initBreakout();
        }

        DisplayUI::beginFrame();
        DisplayUI::drawRetroGames(0, selGame, 0, 0, 0, 0, 0, 0, 0, 0, nullptr, g_spaceSeed);
        DisplayUI::endFrame();
        return;
    }

    // GAME OVER RESTART
    if (g_gameSubState == 2) {
        if (evt == EVT_CLICK || evt == EVT_SHUTTER) {
            triggerChirp(2000, 15);
            g_spaceSeed = (int)millis();
            if (g_curGame == 1) initSnake();
            else if (g_curGame == 2) initPong();
            else if (g_curGame == 3) initFlappy();
            else if (g_curGame == 4) initBreakout();
        }
        DisplayUI::beginFrame();
        if (g_curGame == 1) DisplayUI::drawRetroGames(1, 2, g_gameScore, g_gameHighScore[1], 0, 0, snakeFoodX, snakeFoodY, snakeLen, 0, snakeBody, g_spaceSeed);
        else if (g_curGame == 2) DisplayUI::drawRetroGames(2, 2, g_gameScore, g_gameHighScore[2], 0, pongPlayerY, 0, pongAiY, pongBallX, pongBallY, nullptr, g_spaceSeed);
        else if (g_curGame == 3) DisplayUI::drawRetroGames(3, 2, g_gameScore, g_gameHighScore[3], 50, flappyBirdY, 0, 0, flappyPipeX, flappyPipeGapY, nullptr, g_spaceSeed);
        else if (g_curGame == 4) DisplayUI::drawRetroGames(4, 2, g_gameScore, g_gameHighScore[4], breakoutPadX, 0, 0, 0, breakoutBallX, breakoutBallY, breakoutBricks, g_spaceSeed);
        DisplayUI::endFrame();
        return;
    }

    // 1. CYBER SNAKE GAME LOOP
    if (g_curGame == 1) {
        if (evt == EVT_UP && snakeDir != 1) snakeDir = 3;
        else if (evt == EVT_DOWN && snakeDir != 3) snakeDir = 1;
        else if (evt == EVT_LEFT && snakeDir != 0) snakeDir = 2;
        else if (evt == EVT_RIGHT && snakeDir != 2) snakeDir = 0;

        if (now - g_lastGameTick > 110) {
            g_lastGameTick = now;
            int hx = snakeBody[0] & 0xFF;
            int hy = (snakeBody[0] >> 8) & 0xFF;
            if (snakeDir == 0) hx++;
            else if (snakeDir == 1) hy++;
            else if (snakeDir == 2) hx--;
            else if (snakeDir == 3) hy--;

            // Arena is 27 x 31 grid (8px cells)
            if (hx < 0 || hx >= 27 || hy < 0 || hy >= 31) {
                g_gameSubState = 2; // Game Over
                triggerChirp(400, 50);
                if (g_gameScore > g_gameHighScore[1]) g_gameHighScore[1] = g_gameScore;
            } else {
                // Check self-collision
                for (int i = 0; i < snakeLen; i++) {
                    if ((snakeBody[i] & 0xFF) == hx && ((snakeBody[i] >> 8) & 0xFF) == hy) {
                        g_gameSubState = 2;
                        triggerChirp(400, 50);
                        if (g_gameScore > g_gameHighScore[1]) g_gameHighScore[1] = g_gameScore;
                        break;
                    }
                }
            }

            if (g_gameSubState == 0) {
                // Shift body
                for (int i = snakeLen; i > 0; i--) snakeBody[i] = snakeBody[i - 1];
                snakeBody[0] = hx | (hy << 8);

                // Food collision
                if (hx == snakeFoodX && hy == snakeFoodY) {
                    if (snakeLen < MAX_SNAKE - 1) snakeLen++;
                    g_gameScore += 10;
                    triggerChirp(2400, 10);
                    snakeFoodX = (now ^ hx) % 25 + 1;
                    snakeFoodY = (now ^ hy) % 29 + 1;
                    if (g_gameScore > g_gameHighScore[1]) g_gameHighScore[1] = g_gameScore;
                }
            }
        }

        DisplayUI::beginFrame();
        DisplayUI::drawRetroGames(1, g_gameSubState, g_gameScore, g_gameHighScore[1], 0, 0, snakeFoodX, snakeFoodY, snakeLen, 0, snakeBody, g_spaceSeed);
        DisplayUI::endFrame();
        return;
    }

    // 2. NEON PONG GAME LOOP
    if (g_curGame == 2) {
        if (evt == EVT_UP) pongPlayerY = (pongPlayerY > 40) ? pongPlayerY - 10 : 40;
        else if (evt == EVT_DOWN) pongPlayerY = (pongPlayerY < 240) ? pongPlayerY + 10 : 240;

        if (now - g_lastGameTick > 30) {
            g_lastGameTick = now;
            // Move Ball
            pongBallX += pongBallVx;
            pongBallY += pongBallVy;

            // AI paddle tracking
            if (pongBallY > pongAiY + 17 && pongAiY < 240) pongAiY += 2;
            else if (pongBallY < pongAiY + 17 && pongAiY > 40) pongAiY -= 2;

            // Top/Bottom bounce
            if (pongBallY <= 32 || pongBallY >= 286) {
                pongBallVy = -pongBallVy;
                triggerChirp(1400, 5);
            }

            // Player paddle bounce (Left: x ~ 18)
            if (pongBallX <= 22 && pongBallX >= 12 && pongBallY >= pongPlayerY && pongBallY <= pongPlayerY + 36) {
                pongBallVx = abs(pongBallVx);
                g_gameScore += 5;
                triggerChirp(2000, 8);
                if (g_gameScore > g_gameHighScore[2]) g_gameHighScore[2] = g_gameScore;
            }

            // AI paddle bounce (Right: x ~ 216)
            if (pongBallX >= 214 && pongBallX <= 224 && pongBallY >= pongAiY && pongBallY <= pongAiY + 36) {
                pongBallVx = -abs(pongBallVx);
                triggerChirp(1600, 8);
            }

            // Missed ball
            if (pongBallX < 8) {
                g_gameSubState = 2; // Game Over
                triggerChirp(400, 40);
            } else if (pongBallX > 230) {
                // Point won, reset ball
                g_gameScore += 20;
                pongBallX = 120; pongBallY = 150;
                pongBallVx = -3;
                triggerChirp(2600, 15);
                if (g_gameScore > g_gameHighScore[2]) g_gameHighScore[2] = g_gameScore;
            }
        }

        DisplayUI::beginFrame();
        DisplayUI::drawRetroGames(2, g_gameSubState, g_gameScore, g_gameHighScore[2], 0, pongPlayerY, 0, pongAiY, pongBallX, pongBallY, nullptr, g_spaceSeed);
        DisplayUI::endFrame();
        return;
    }

    // 3. CYBER FLAPPY GAME LOOP
    if (g_curGame == 3) {
        if (evt == EVT_UP || evt == EVT_CLICK || evt == EVT_SHUTTER) {
            flappyVy = -4.5f;
            triggerChirp(1900, 8);
        }

        if (now - g_lastGameTick > 30) {
            g_lastGameTick = now;
            flappyVy += 0.35f;
            if (flappyVy > 6.0f) flappyVy = 6.0f;
            flappyBirdY += (int)flappyVy;
            flappyPipeX -= 3;

            // Pipe wrap & scoring
            if (flappyPipeX < -25) {
                flappyPipeX = 230;
                flappyPipeGapY = 50 + (now % 100);
                g_gameScore += 10;
                triggerChirp(2400, 10);
                if (g_gameScore > g_gameHighScore[3]) g_gameHighScore[3] = g_gameScore;
            }

            // Ceiling / Ground collision (arena: y=24, h=274)
            if (flappyBirdY <= 44 || flappyBirdY >= 290) {
                g_gameSubState = 2;
                triggerChirp(400, 50);
                if (g_gameScore > g_gameHighScore[3]) g_gameHighScore[3] = g_gameScore;
            }

            // Pipe obstacle collision (birdX = 50, pipeW = 20, gapH = 68)
            if (flappyBirdY > 44 && flappyBirdY < 290) {
                if (50 + 5 >= flappyPipeX && 50 - 5 <= flappyPipeX + 20) {
                    if (flappyBirdY - 5 < flappyPipeGapY || flappyBirdY + 5 > flappyPipeGapY + 68) {
                        g_gameSubState = 2;
                        triggerChirp(400, 50);
                        if (g_gameScore > g_gameHighScore[3]) g_gameHighScore[3] = g_gameScore;
                    }
                }
            }
        }

        DisplayUI::beginFrame();
        DisplayUI::drawRetroGames(3, g_gameSubState, g_gameScore, g_gameHighScore[3], 50, flappyBirdY, 0, 0, flappyPipeX, flappyPipeGapY, nullptr, g_spaceSeed);
        DisplayUI::endFrame();
        return;
    }

    // 4. NEON BREAKOUT GAME LOOP
    if (g_curGame == 4) {
        if (evt == EVT_LEFT) breakoutPadX = (breakoutPadX > 14) ? breakoutPadX - 10 : 14;
        else if (evt == EVT_RIGHT) breakoutPadX = (breakoutPadX < 240 - 14 - 38) ? breakoutPadX + 10 : 240 - 14 - 38;

        if (now - g_lastGameTick > 25) {
            g_lastGameTick = now;
            breakoutBallX += breakoutBallVx;
            breakoutBallY += breakoutBallVy;

            // Wall bounces
            if (breakoutBallX <= 12) {
                breakoutBallX = 12; breakoutBallVx = abs(breakoutBallVx);
                triggerChirp(1400, 4);
            } else if (breakoutBallX >= 228) {
                breakoutBallX = 228; breakoutBallVx = -abs(breakoutBallVx);
                triggerChirp(1400, 4);
            }

            if (breakoutBallY <= 42) {
                breakoutBallY = 42; breakoutBallVy = abs(breakoutBallVy);
                triggerChirp(1400, 4);
            }

            // Paddle bounce (padY = 286, padW = 38)
            int padY = 286;
            if (breakoutBallY >= padY - 4 && breakoutBallY <= padY + 4 &&
                breakoutBallX >= breakoutPadX - 4 && breakoutBallX <= breakoutPadX + 42) {
                breakoutBallVy = -abs(breakoutBallVy);
                int hitOff = breakoutBallX - (breakoutPadX + 19);
                if (hitOff < -8) breakoutBallVx = -3;
                else if (hitOff > 8) breakoutBallVx = 3;
                triggerChirp(1900, 8);
            }

            // Brick destruction
            int aliveCount = 0;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 8; c++) {
                    int bidx = r * 8 + c;
                    if (breakoutBricks[bidx]) {
                        aliveCount++;
                        int bx = 18 + c * 26;
                        int by = 52 + r * 14;
                        if (breakoutBallX >= bx - 3 && breakoutBallX <= bx + 25 &&
                            breakoutBallY >= by - 3 && breakoutBallY <= by + 13) {
                            breakoutBricks[bidx] = 0;
                            breakoutBallVy = -breakoutBallVy;
                            g_gameScore += 15;
                            triggerChirp(2200, 8);
                            if (g_gameScore > g_gameHighScore[4]) g_gameHighScore[4] = g_gameScore;
                            aliveCount--;
                        }
                    }
                }
            }

            // Wave clear bonus
            if (aliveCount == 0) {
                for (int i = 0; i < 24; i++) breakoutBricks[i] = 1;
                g_gameScore += 100;
                triggerChirp(2700, 20);
            }

            // Bottom miss (Game Over)
            if (breakoutBallY >= 292) {
                g_gameSubState = 2;
                triggerChirp(400, 50);
                if (g_gameScore > g_gameHighScore[4]) g_gameHighScore[4] = g_gameScore;
            }
        }

        DisplayUI::beginFrame();
        DisplayUI::drawRetroGames(4, g_gameSubState, g_gameScore, g_gameHighScore[4], breakoutPadX, 0, 0, 0, breakoutBallX, breakoutBallY, breakoutBricks, g_spaceSeed);
        DisplayUI::endFrame();
        return;
    }
}

// ============================================================
// 3. SD FILE EXPLORER
// ============================================================
#define MAX_FE 128
static char fePath[96] = "/";
static SdEntry feList[MAX_FE];
static int feCount = 0;
static int feSel = 0;
static int feScroll = 0;
static bool feViewing = false;
static char feViewPath[128];
static bool feConfirmDelete = false;
static bool feConfirmYes = false;

static void refreshFE() {
    feCount = SDManager::listDir(fePath, feList, MAX_FE);
    if (feSel >= feCount) feSel = feCount > 0 ? feCount - 1 : 0;
    feScroll = 0;
}

static void feGoUp() {
    if (strcmp(fePath, "/") == 0) { switchMode(MODE_MAIN_MENU); return; }
    char* slash = strrchr(fePath, '/');
    if (slash == fePath) fePath[1] = '\0';
    else if (slash) *slash = '\0';
    feSel = 0;
    refreshFE();
}

static void runFileExplorerMode(InputEvent evt) {
    static bool entered = false;
    static int lastMode = -1;
    if (lastMode != MODE_FILE_EXPLORER) { strcpy(fePath, "/"); feSel = 0; feViewing = false; refreshFE(); lastMode = MODE_FILE_EXPLORER; }
    (void)entered;

    const int VISIBLE = 10;

    static bool feRenaming = false;
    static char feRenameBuf[32] = {0};
    static int feKbdRow = -1;
    static int feKbdCol = 1;

    if (feRenaming) {
        if (evt == EVT_MENU || evt == EVT_BACK) {
            feRenaming = false;
            triggerChirp(1000, 15);
            return;
        }

        if (evt == EVT_UP) {
            if (feKbdRow == -1) feKbdRow = 3;
            else if (feKbdRow == 0) { feKbdRow = -1; feKbdCol = (feKbdCol >= 5) ? 1 : 0; }
            else feKbdRow--;
            triggerChirp(1400, 5);
        } else if (evt == EVT_DOWN) {
            if (feKbdRow == -1) { feKbdRow = 0; feKbdCol = (feKbdCol == 1) ? 8 : 4; }
            else if (feKbdRow == 3) { feKbdRow = -1; feKbdCol = (feKbdCol >= 5) ? 1 : 0; }
            else feKbdRow++;
            triggerChirp(1400, 5);
        } else if (evt == EVT_LEFT) {
            if (feKbdRow == -1) feKbdCol = (feKbdCol == 1) ? 0 : 1;
            else feKbdCol = (feKbdCol - 1 + 10) % 10;
            triggerChirp(1400, 5);
        } else if (evt == EVT_RIGHT) {
            if (feKbdRow == -1) feKbdCol = (feKbdCol == 0) ? 1 : 0;
            else feKbdCol = (feKbdCol + 1) % 10;
            triggerChirp(1400, 5);
        }

        if (evt == EVT_CLICK) {
            if (feKbdRow == -1 && feKbdCol == 0) {
                int len = strlen(feRenameBuf);
                if (len > 0) { feRenameBuf[len - 1] = '\0'; triggerChirp(1200, 8); }
            } else if (feKbdRow == -1 && feKbdCol == 1) {
                // Save / apply rename
                if (strlen(feRenameBuf) > 0 && feCount > 0) {
                    char oldPath[160], newPath[160];
                    snprintf(oldPath, sizeof(oldPath), "%s%s%s", fePath, (strcmp(fePath,"/")==0?"":"/"), feList[feSel].name);
                    const char* dot = strrchr(feList[feSel].name, '.');
                    const char* ext = dot ? dot : "";
                    snprintf(newPath, sizeof(newPath), "%s%s%s%s", fePath, (strcmp(fePath,"/")==0?"":"/"), feRenameBuf, ext);
                    SD_MMC.rename(oldPath, newPath);
                    refreshFE();
                    triggerChirp(2400, 20);
                }
                feRenaming = false;
                return;
            } else if (feKbdRow >= 0 && feKbdRow < 4 && feKbdCol >= 0 && feKbdCol < 10) {
                int len = strlen(feRenameBuf);
                if (len < 24) {
                    feRenameBuf[len] = s_kbdRows[feKbdRow][feKbdCol];
                    feRenameBuf[len + 1] = '\0';
                    triggerChirp(1900, 6);
                }
            }
        } else if (evt == EVT_SHUTTER) {
            if (strlen(feRenameBuf) > 0 && feCount > 0) {
                char oldPath[160], newPath[160];
                snprintf(oldPath, sizeof(oldPath), "%s%s%s", fePath, (strcmp(fePath,"/")==0?"":"/"), feList[feSel].name);
                const char* dot = strrchr(feList[feSel].name, '.');
                const char* ext = dot ? dot : "";
                snprintf(newPath, sizeof(newPath), "%s%s%s%s", fePath, (strcmp(fePath,"/")==0?"":"/"), feRenameBuf, ext);
                SD_MMC.rename(oldPath, newPath);
                refreshFE();
                triggerChirp(2400, 20);
            }
            feRenaming = false;
            return;
        }

        DisplayUI::beginFrame();
        DisplayUI::drawFileExplorer(fePath, feList, feCount, feSel, feScroll, SDManager::getFreeSpaceMB(), SDManager::getTotalSpaceMB());
        const char* dot = strrchr(feList[feSel].name, '.');
        DisplayUI::drawNamingKeyboardDialog(feRenameBuf, feKbdRow, feKbdCol, dot ? dot + 1 : "", "RENAME FILE", "JOY:Move CLICK:Type SHUTTER:Save ESC:Cancel");
        DisplayUI::endFrame();
        return;
    }

    if (feConfirmDelete) {
        if (evt == EVT_LEFT || evt == EVT_RIGHT) { feConfirmYes = !feConfirmYes; }
        else if (evt == EVT_CLICK) {
            if (feConfirmYes) {
                char full[160];
                snprintf(full, sizeof(full), "%s%s%s", fePath, (strcmp(fePath,"/")==0?"":"/"), feList[feSel].name);
                SD_MMC.remove(full);
                triggerChirp(800, 30);
                refreshFE();
            }
            feConfirmDelete = false;
        } else if (evt == EVT_MENU || evt == EVT_BACK) { feConfirmDelete = false; }
        DisplayUI::beginFrame();
        DisplayUI::drawFileExplorer(fePath, feList, feCount, feSel, feScroll, SDManager::getFreeSpaceMB(), SDManager::getTotalSpaceMB());
        DisplayUI::drawConfirm("DELETE FILE", feList[feSel].name, feConfirmYes);
        DisplayUI::endFrame();
        return;
    }

    if (feViewing) {
        if (evt == EVT_MENU || evt == EVT_BACK || evt == EVT_LEFT) { feViewing = false; }
        DisplayUI::beginFrame();
        DisplayUI::drawGalleryScreen(feViewPath, 0, 1, SD_MMC.exists(feViewPath), false);
        DisplayUI::endFrame();
        return;
    }

    if (evt == EVT_MENU || evt == EVT_BACK) { lastMode = -1; switchMode(MODE_MAIN_MENU); return; }
    if (evt == EVT_LEFT) { feGoUp(); if (g_state.mode != MODE_FILE_EXPLORER) { lastMode = -1; } return; }

    if (evt == EVT_UP)   { if (feCount) feSel = (feSel - 1 + feCount) % feCount; triggerChirp(1400, 6); }
    if (evt == EVT_DOWN) { if (feCount) feSel = (feSel + 1) % feCount; triggerChirp(1400, 6); }
    if (feSel < feScroll) feScroll = feSel;
    if (feSel >= feScroll + VISIBLE) feScroll = feSel - VISIBLE + 1;

    // SHUTTER HOLD: rename highlighted file using virtual keyboard
    if (evt == EVT_SHUTTER_HOLD && feCount > 0 && !feList[feSel].isDir) {
        feRenaming = true;
        char base[32];
        strncpy(base, feList[feSel].name, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char* dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        strncpy(feRenameBuf, base, sizeof(feRenameBuf) - 1);
        feRenameBuf[sizeof(feRenameBuf) - 1] = '\0';
        feKbdRow = -1; feKbdCol = 1;
        triggerChirp(1800, 15);
        return;
    }

    if (evt == EVT_SHUTTER && feCount > 0 && !feList[feSel].isDir) {
        feConfirmDelete = true; feConfirmYes = false; triggerChirp(1800, 10);
    }

    if (evt == EVT_CLICK && feCount > 0) {
        if (feList[feSel].isDir) {
            size_t len = strlen(fePath);
            if (strcmp(fePath, "/") != 0 && len < sizeof(fePath) - 1) strcat(fePath, "/");
            strncat(fePath, feList[feSel].name, sizeof(fePath) - strlen(fePath) - 1);
            feSel = 0; refreshFE(); triggerChirp(1700, 8);
        } else {
            bool v;
            if (isMediaName(feList[feSel].name, v) && !v) {
                snprintf(feViewPath, sizeof(feViewPath), "%s%s%s", fePath, (strcmp(fePath,"/")==0?"":"/"), feList[feSel].name);
                feViewing = true; triggerChirp(2000, 10);
            } else triggerChirp(900, 10);
        }
    }

    DisplayUI::beginFrame();
    DisplayUI::drawFileExplorer(fePath, feList, feCount, feSel, feScroll, SDManager::getFreeSpaceMB(), SDManager::getTotalSpaceMB());
    DisplayUI::endFrame();
}



// ============================================================
// 4. 2.4GHz RF ANALYZER (buffered, ghost-free)
// ============================================================

static void runNrfMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK) { switchMode(MODE_MAIN_MENU); return; }

    static int8_t spectrum[126];
    NRFScanner::getSpectrum(spectrum, 126);
    NRFScanner::updateWaterfall();

    DisplayUI::beginFrame();
    auto* d = DisplayUI::canvas();
    DisplayUI::drawTopBar("2.4GHz RF ANALYZER", THEME_AMBER, g_state.batteryPercent);

    d->drawRect(4, 24, SCREEN_WIDTH - 8, 108, THEME_CYAN);
    for (int i = 0; i < 126; i++) {
        int x = 6 + (i * (SCREEN_WIDTH - 12)) / 126;
        int h = spectrum[i] + 100;
        if (h < 2) h = 2; if (h > 104) h = 104;
        uint16_t color = h > 75 ? THEME_CRIMSON : (h > 40 ? THEME_AMBER : THEME_GREEN);
        d->drawFastVLine(x, 130 - h, h, color);
    }

    uint8_t* wf = NRFScanner::getWaterfallBuffer();
    uint8_t wfW = NRFScanner::getWaterfallWidth();
    uint8_t wfH = NRFScanner::getWaterfallHeight();
    if (wf) {
        d->drawRect(4, 136, SCREEN_WIDTH - 8, wfH + 4, THEME_DIM_CYAN);
        for (int y = 0; y < wfH; y++) {
            for (int x = 0; x < wfW; x++) {
                uint8_t intensity = wf[y * wfW + x];
                if (intensity > 20) {
                    uint16_t col = intensity > 120 ? THEME_CRIMSON : (intensity > 60 ? THEME_AMBER : THEME_GREEN);
                    int px = 6 + (x * (SCREEN_WIDTH - 12)) / wfW;
                    d->drawPixel(px, 138 + y, col);
                }
            }
        }
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "SWEEP CH %u  RSSI %d dBm", NRFScanner::getCurrentChannel(), NRFScanner::getCurrentRSSI());
    d->setTextColor(THEME_CYAN, THEME_BG);
    d->setCursor(8, 210);
    d->print(buf);
    d->setTextColor(THEME_GREY, THEME_BG);
    d->setCursor(8, 224);
    d->print("2400 - 2525 MHz | 126 CH");

    DisplayUI::drawBottomBar("MENU:Back");
    DisplayUI::endFrame();
}

// ============================================================
// 5. WI-FI SUITE (buffered)
// ============================================================
static void runWifiMode(InputEvent evt) {
    static uint32_t lastDraw = 0;
    static uint8_t attackSelect = 0;
    static int apSelectedIdx = 0;
    static uint32_t deauthUntilMs = 0;
    static uint32_t hsToastUntil = 0;

    if (evt == EVT_MENU || evt == EVT_BACK) {
        WiFiAnalyzer::stopAttack();
        WiFiAnalyzer::stopPCAP();
        switchMode(MODE_MAIN_MENU);
        return;
    }
    if (evt == EVT_RIGHT) { switchMode(MODE_BLE_ANALYZER); return; }

    uint8_t count = WiFiAnalyzer::getAPCount();

    if (evt == EVT_UP) {
        if (count > 0) apSelectedIdx = (apSelectedIdx - 1 + count) % count;
        triggerChirp(1400, 4);
    } else if (evt == EVT_DOWN) {
        if (count > 0) apSelectedIdx = (apSelectedIdx + 1) % count;
        triggerChirp(1400, 4);
    }

    // SHUTTER: Targeted Deauth & Handshake Intercept on selected AP
    if (evt == EVT_SHUTTER) {
        if (count > 0 && apSelectedIdx < count) {
            DiscoveredAP targetAp;
            if (WiFiAnalyzer::getAP(apSelectedIdx, targetAp)) {
                WiFiAnalyzer::enableHop(false);
                WiFiAnalyzer::setChannel(targetAp.channel);
                WiFiAnalyzer::sendTargetedDeauth(targetAp.bssid, targetAp.channel);
                deauthUntilMs = millis() + 1500;
                triggerChirp(2200, 25);
            }
        } else {
            // Toggle global PCAP stream if no AP selected
            if (!WiFiAnalyzer::isPCAPActive() && SDManager::isMounted()) {
                static uint32_t pcapIdx = 0;
                char pcapPath[64];
                snprintf(pcapPath, sizeof(pcapPath), "%s/dump_%04u.pcap", PATH_PCAP, (unsigned)pcapIdx++);
                WiFiAnalyzer::startPCAP(pcapPath); triggerChirp(1500, 15);
            } else if (WiFiAnalyzer::isPCAPActive()) {
                WiFiAnalyzer::stopPCAP(); triggerChirp(900, 15);
            }
        }
    }

    if (evt == EVT_CLICK) {
        attackSelect = (attackSelect + 1) % 5;
        triggerChirp(1800, 10);
        switch (attackSelect) {
            case 0: WiFiAnalyzer::stopAttack(); break;
            case 1: WiFiAnalyzer::startAttack(WIFI_ATTACK_BEACON_SPAM); break;
            case 2: WiFiAnalyzer::startAttack(WIFI_ATTACK_RICKROLL); break;
            case 3: WiFiAnalyzer::startAttack(WIFI_ATTACK_DEAUTH_BROADCAST); break;
            case 4: WiFiAnalyzer::startAttack(WIFI_ATTACK_EAPOL_ROGUE); break;
        }
    }

    // Check for newly intercepted 4-way handshake
    if (WiFiAnalyzer::hasNewHandshake()) {
        WiFiAnalyzer::clearHandshakeFlag();
        hsToastUntil = millis() + 3500;
        triggerChirp(2800, 40);
    }

    if (millis() - lastDraw < 100) return;
    lastDraw = millis();

    DisplayUI::beginFrame();
    auto* d = DisplayUI::canvas();
    DisplayUI::drawTopBar("WI-FI 802.11 MONITOR", THEME_GREEN, g_state.batteryPercent);

    const WifiStats& s = WiFiAnalyzer::getStats();
    d->setTextSize(1);
    d->setTextColor(THEME_CYAN, THEME_BG);
    d->setCursor(6, 24);  d->printf("CH %u | Pkts %u | Deauth Sent: %u", WiFiAnalyzer::getChannel(), (unsigned)s.totalPackets, (unsigned)s.attacksSent);
    d->setCursor(6, 36);  d->printf("Deauth Rx %u  EAPOL %u  PMKID %u", (unsigned)s.deauthCount, (unsigned)s.eapolCount, (unsigned)s.pmkidCount);

    // Active Attack Status Box
    const char* atkNames[] = {"PASSIVE MONITOR", "BEACON SPAM", "RICKROLL TEST", "BROADCAST DEAUTH", "ROGUE EAPOL"};
    uint16_t atkColor = attackSelect == 0 ? THEME_GREEN : (attackSelect == 4 ? THEME_AMBER : THEME_CYAN);
    d->fillRoundRect(4, 48, SCREEN_WIDTH - 8, 18, 2, THEME_PANEL);
    d->drawRoundRect(4, 48, SCREEN_WIDTH - 8, 18, 2, atkColor);
    d->setTextColor(atkColor, THEME_PANEL);
    d->setCursor(8, 53);
    d->printf("MODE: %-18s  [CLICK:Cycle Mode]", atkNames[attackSelect]);

    // Deauth Active Warning Banner
    if (millis() < deauthUntilMs) {
        d->fillRoundRect(4, 68, SCREEN_WIDTH - 8, 16, 2, THEME_CRIMSON);
        d->setTextColor(THEME_WHITE, THEME_CRIMSON);
        d->setCursor(14, 72);
        d->print("! TARGET DEAUTH PULSE ACTIVE - SNIFFING HANDSHAKE !");
    } else {
        d->setTextColor(THEME_AMBER, THEME_BG);
        d->setCursor(6, 72);
        d->printf("DISCOVERED ACCESS POINTS (%u): [JOY:Select SNAP:Deauth]", count);
    }

    // Handshake Captured Victory Toast Overlay
    if (millis() < hsToastUntil) {
        int tw = 224, th = 28;
        int tx = (SCREEN_WIDTH - tw) / 2;
        int ty = 86;
        d->fillRoundRect(tx, ty, tw, th, 3, THEME_PANEL);
        d->drawRoundRect(tx, ty, tw, th, 3, THEME_GREEN);
        d->setTextColor(THEME_GREEN, THEME_PANEL);
        d->setCursor(tx + 8, ty + 6);
        d->printf("! WPA2 HANDSHAKE CAPTURED: %s !", WiFiAnalyzer::getLastHandshakeSSID());
        d->setTextColor(THEME_WHITE, THEME_PANEL);
        d->setCursor(tx + 8, ty + 16);
        d->print("SAVED TO SD -> /HANDSHAKES/*.PCAP");
    }

    int yPos = 88;
    const char* secNames[] = { "OPEN", "WEP", "WPA", "WPA2", "WPA3" };
    int maxVisible = 10;
    int scrollOffset = 0;
    if (apSelectedIdx >= maxVisible) scrollOffset = apSelectedIdx - maxVisible + 1;

    for (uint8_t i = scrollOffset; i < count && i < scrollOffset + maxVisible; i++) {
        DiscoveredAP ap;
        if (WiFiAnalyzer::getAP(i, ap)) {
            bool isSel = (i == apSelectedIdx);
            if (isSel) {
                d->fillRect(4, yPos - 1, SCREEN_WIDTH - 8, 14, THEME_SURFACE);
                d->drawRect(4, yPos - 1, SCREEN_WIDTH - 8, 14, THEME_CYAN);
            }
            d->setCursor(6, yPos + 2);
            uint8_t sIdx = ap.security <= 4 ? ap.security : 0;
            uint16_t secCol = (sIdx == 0) ? THEME_CRIMSON : (sIdx >= 3 ? THEME_GREEN : THEME_CYAN);
            d->setTextColor(secCol, isSel ? THEME_SURFACE : THEME_BG);
            d->printf("[%s] ", secNames[sIdx]);

            d->setTextColor(isSel ? THEME_WHITE : THEME_GREY, isSel ? THEME_SURFACE : THEME_BG);
            char shortSsid[16]; strncpy(shortSsid, ap.ssid, 15); shortSsid[15] = '\0';
            d->printf("CH%02u (%4d) %s", ap.channel, ap.rssi, strlen(shortSsid) > 0 ? shortSsid : "<Hidden>");
            yPos += 15;
        }
    }

    DisplayUI::drawBottomBar("JOY:Select  SNAP:Target Deauth  RIGHT:BLE  MENU:Exit");
    DisplayUI::endFrame();
}

static void runBleMode(InputEvent evt) {
    static uint32_t lastDraw = 0;
    static uint8_t bleAtkSelect = 0;
    static int bleSelectedIdx = 0;
    static bool foxHunting = false;

    if (evt == EVT_MENU || evt == EVT_BACK) {
        if (foxHunting) { foxHunting = false; triggerChirp(1200, 10); return; }
        BLEAnalyzer::stopSpam(); BLEAnalyzer::stopScan(); switchMode(MODE_MAIN_MENU); return;
    }
    if (evt == EVT_LEFT) { switchMode(MODE_WIFI_ANALYZER); return; }

    uint16_t devCount = BLEAnalyzer::getDeviceCount();

    if (evt == EVT_UP) {
        if (devCount > 0) bleSelectedIdx = (bleSelectedIdx - 1 + devCount) % devCount;
        triggerChirp(1400, 4);
    } else if (evt == EVT_DOWN) {
        if (devCount > 0) bleSelectedIdx = (bleSelectedIdx + 1) % devCount;
        triggerChirp(1400, 4);
    }

    // SHUTTER: Lock onto highlighted device for Fox-Hunt Signal Seeking
    if (evt == EVT_SHUTTER && devCount > 0) {
        BleDeviceRecord targetDev;
        if (BLEAnalyzer::getDevice(bleSelectedIdx, targetDev)) {
            foxHunting = !foxHunting;
            if (foxHunting) {
                TacticalSuite::setFoxTarget(targetDev.mac, targetDev.name, FOX_TYPE_BLE_DEVICE, 37);
                triggerChirp(2400, 30);
            } else {
                TacticalSuite::clearFoxTarget();
                triggerChirp(900, 15);
            }
        }
    }

    if (evt == EVT_CLICK) {
        bleAtkSelect = (bleAtkSelect + 1) % 7;
        triggerChirp(1800, 10);
        if (bleAtkSelect == 0) { BLEAnalyzer::stopSpam(); BLEAnalyzer::startScan(); }
        else BLEAnalyzer::startSpam((BleSpamType)(bleAtkSelect - 1));
    }

    // Audio Geiger Pulse if Fox-Hunt is active
    if (foxHunting) {
        BleDeviceRecord curDev;
        if (BLEAnalyzer::getDevice(bleSelectedIdx, curDev)) {
            TacticalSuite::updateFoxRssi(curDev.mac, curDev.rssi);
        }
        uint16_t gFreq, gDur;
        if (TacticalSuite::checkGeigerChirp(gFreq, gDur)) {
            triggerChirp(gFreq, gDur);
        }
    }

    if (millis() - lastDraw < 100) return;
    lastDraw = millis();

    DisplayUI::beginFrame();
    auto* d = DisplayUI::canvas();

    if (foxHunting) {
        const FoxHuntSession& fs = TacticalSuite::getFoxSession();
        static float radarAng = 0;
        radarAng = fmodf(radarAng + 8.0f, 360.0f);
        DisplayUI::drawTacticalFoxHunt(fs.targetName, fs.targetMac, fs.currentRssi, fs.filteredRssi,
                                      fs.peakRssi, fs.estimatedDistanceM, fs.channel, fs.isLocked, radarAng);
        DisplayUI::endFrame();
        return;
    }

    DisplayUI::drawTopBar("BLE PROXIMITY SCANNER", THEME_AMBER, g_state.batteryPercent);

    const char* spamNames[] = { "DISCOVERY SCANNER", "APPLE PROXIMITY TEST", "GOOGLE FAST-PAIR TEST", "SAMSUNG BUDS TEST", "MS SWIFT-PAIR TEST", "FLIPPER TEST", "ROTATE ALL PRESETS" };
    uint16_t boxCol = bleAtkSelect == 0 ? THEME_CYAN : THEME_AMBER;
    d->fillRoundRect(4, 25, SCREEN_WIDTH - 8, 22, 3, THEME_PANEL);
    d->drawRoundRect(4, 25, SCREEN_WIDTH - 8, 22, 3, boxCol);
    d->setTextColor(boxCol, THEME_PANEL);
    d->setCursor(8, 32);
    d->printf("TEST: %-22s [CLICK:Cycle]", spamNames[bleAtkSelect]);

    if (BLEAnalyzer::isSpamming()) {
        d->setCursor(8, 60);
        d->setTextColor(THEME_GREEN, THEME_BG);
        d->printf("TX Adv Frames: %u", (unsigned)BLEAnalyzer::getPacketsSent());
        DisplayUI::drawBottomBar("CLICK:Mode LEFT:Wi-Fi MENU:Exit");
        DisplayUI::endFrame();
        return;
    }

    d->setTextColor(THEME_CYAN, THEME_BG);
    d->setCursor(6, 54);
    d->printf("DISCOVERED BLE DEVICES (%u): [JOY:Select SNAP:FoxHunt]", devCount);

    int maxVisible = 12;
    int scrollOffset = 0;
    if (bleSelectedIdx >= maxVisible) scrollOffset = bleSelectedIdx - maxVisible + 1;
    int y = 70;

    for (uint16_t i = scrollOffset; i < devCount && i < scrollOffset + maxVisible; i++) {
        BleDeviceRecord dev;
        if (BLEAnalyzer::getDevice(i, dev)) {
            bool isSel = (i == bleSelectedIdx);
            if (isSel) {
                d->fillRect(4, y - 1, SCREEN_WIDTH - 8, 14, THEME_SURFACE);
                d->drawRect(4, y - 1, SCREEN_WIDTH - 8, 14, THEME_CYAN);
            }

            // Detect skimmers, AirTags, and Flippers
            bool isSkimmer = (strstr(dev.name, "HC-0") || strstr(dev.name, "BT-0") || strstr(dev.name, "RN-42") || strstr(dev.name, "SPP"));
            bool isAirTag = (dev.deviceType == 1 || strstr(dev.name, "AirTag") || strstr(dev.name, "FindMy"));
            bool isFlipper = (dev.deviceType == 5 || strstr(dev.name, "flipper"));

            if (isSkimmer) {
                d->setTextColor(THEME_CRIMSON, isSel ? THEME_SURFACE : THEME_BG);
                d->print(" [SKIMMER!]");
            } else if (isAirTag) {
                d->setTextColor(THEME_AMBER, isSel ? THEME_SURFACE : THEME_BG);
                d->print(" [AIRTAG]");
            } else if (isFlipper) {
                d->setTextColor(THEME_CYAN, isSel ? THEME_SURFACE : THEME_BG);
                d->print(" [FLIPPER]");
            } else {
                d->setTextColor(dev.deviceType != 0 ? THEME_GREEN : THEME_GREY, isSel ? THEME_SURFACE : THEME_BG);
                d->printf("[%3ddBm] ", dev.rssi);
            }

            d->setTextColor(isSel ? THEME_WHITE : THEME_WHITE, isSel ? THEME_SURFACE : THEME_BG);
            d->printf("%02X:%02X %-14s", dev.mac[4], dev.mac[5], dev.name);
            y += 14;
        }
    }

    DisplayUI::drawBottomBar("JOY:Select  SNAP:FoxHunt Seeker  LEFT:Wi-Fi  MENU:Exit");
    DisplayUI::endFrame();
}

// ============================================================
// 5B. TACTICAL DRONE REMOTE-ID RADAR (FAA ASTM F3411)
// ============================================================
static void runRemoteIdMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK) {
        TacticalSuite::stopDroneScan();
        WiFiAnalyzer::stopPromiscuous();
        switchMode(MODE_MAIN_MENU);
        return;
    }
    static int selDroneIdx = 0;
    static float radarAngle = 0.0f;
    radarAngle += 0.08f;
    if (radarAngle >= 6.283f) radarAngle = 0.0f;

    uint8_t count = TacticalSuite::getDroneCount();
    if (evt == EVT_UP) {
        if (count > 0) selDroneIdx = (selDroneIdx - 1 + count) % count;
        triggerChirp(1400, 5);
    } else if (evt == EVT_DOWN) {
        if (count > 0) selDroneIdx = (selDroneIdx + 1) % count;
        triggerChirp(1400, 5);
    } else if (evt == EVT_SHUTTER) {
        triggerChirp(2400, 25);
    }

    DisplayUI::beginFrame();
    DisplayUI::drawTacticalDroneRadar(count, selDroneIdx, radarAngle);
    DisplayUI::endFrame();
}

// ============================================================
// 5C. SIGNAL FOX-HUNT DIRECTION SEEKER
// ============================================================
static void runFoxHuntMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK) {
        TacticalSuite::stopFoxHunt();
        switchMode(MODE_MAIN_MENU);
        return;
    }
    FoxHuntSession fox = TacticalSuite::getFoxSession();
    if (evt == EVT_CLICK) {
        TacticalSuite::clearFoxTarget();
        triggerChirp(1800, 8);
    }

    DisplayUI::beginFrame();
    DisplayUI::drawTacticalFoxHunt(fox.targetName, fox.targetMac, fox.currentRssi, fox.filteredRssi,
                                   fox.peakRssi, fox.channel, fox.isLocked, fox.estimatedDistanceM, fox.targetType);
    DisplayUI::endFrame();
}

// ============================================================
// 5D. COVERT SURVEILLANCE & SKIMMER SCANNER
// ============================================================
static void runSkimmerMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK) {
        TacticalSuite::stopSurveillanceScan();
        WiFiAnalyzer::stopPromiscuous();
        switchMode(MODE_MAIN_MENU);
        return;
    }
    static int selNodeIdx = 0;
    uint8_t count = TacticalSuite::getSurveillanceCount();
    if (evt == EVT_UP) {
        if (count > 0) selNodeIdx = (selNodeIdx - 1 + count) % count;
        triggerChirp(1400, 5);
    } else if (evt == EVT_DOWN) {
        if (count > 0) selNodeIdx = (selNodeIdx + 1) % count;
        triggerChirp(1400, 5);
    }

    DisplayUI::beginFrame();
    DisplayUI::drawTacticalSkimmerScanner(count, selNodeIdx, millis());
    DisplayUI::endFrame();
}

// ============================================================
// 6. SUB-GHZ 433MHz
// ============================================================
static void runSubGhzMode(InputEvent evt) {
    static uint32_t simulatedSerial = 0x5A3C1;
    static uint8_t simulatedBtn = 1;
    static char statusLine[48] = "";
    static uint16_t statusColor = THEME_GREY;

    if (evt == EVT_MENU || evt == EVT_BACK) { switchMode(MODE_MAIN_MENU); return; }

    if (evt == EVT_RIGHT) {
        SubGhzSignal emu;
        SubGhzEngine::synthesizeEV1527(simulatedSerial, simulatedBtn, &emu);
        SubGhzEngine::transmit(&emu);
        snprintf(statusLine, sizeof(statusLine), "TX EV1527 0x%05X btn %d", (unsigned)simulatedSerial, simulatedBtn);
        statusColor = THEME_GREEN;
        simulatedBtn = (simulatedBtn % 4) + 1;
    }

    if (evt == EVT_SHUTTER) {
        snprintf(statusLine, sizeof(statusLine), "Capturing 433.92 MHz...");
        statusColor = THEME_AMBER;
        SubGhzEngine::startCapture();
        vTaskDelay(pdMS_TO_TICKS(1800));
        SubGhzEngine::stopCapture();
        if (SubGhzEngine::hasCapture()) {
            SubGhzSignal* sig = SubGhzEngine::getCapture();
            uint32_t serial = 0; uint8_t btn = 0;
            if (SubGhzEngine::decodeEV1527(sig, &serial, &btn))
                snprintf(statusLine, sizeof(statusLine), "RX %u pulses S/N 0x%X b%d", sig->count, (unsigned)serial, btn);
            else
                snprintf(statusLine, sizeof(statusLine), "RX %u raw pulses", sig->count);
            statusColor = THEME_GREEN;
            static uint32_t sgCounter = 0;
            char fname[32]; snprintf(fname, sizeof(fname), "SG_%04u.sub", (unsigned)sgCounter++);
            SubGhzEngine::saveToSD(sig, fname);
        } else { snprintf(statusLine, sizeof(statusLine), "No signal captured"); statusColor = THEME_CRIMSON; }
    }

    if (evt == EVT_CLICK && SubGhzEngine::hasCapture()) {
        SubGhzEngine::transmit(SubGhzEngine::getCapture());
        snprintf(statusLine, sizeof(statusLine), "Replaying captured train");
        statusColor = THEME_CRIMSON;
    }

    DisplayUI::beginFrame();
    auto* d = DisplayUI::canvas();
    DisplayUI::drawTopBar("SUB-GHZ 433MHz SUITE", THEME_AMBER, g_state.batteryPercent);

    d->setTextColor(THEME_CYAN, THEME_BG);
    d->setCursor(8, 26); d->println("RX:GPIO44  TX:GPIO43  ASK/OOK");
    d->setTextColor(THEME_WHITE, THEME_BG);
    d->setCursor(8, 42); d->println("SHUTTER: Sniff raw pulses");
    d->setCursor(8, 54); d->println("CLICK  : Replay last capture");
    d->setCursor(8, 66); d->println("RIGHT  : Synthesize EV1527");

    d->drawRect(4, 84, SCREEN_WIDTH - 8, 150, THEME_CYAN);
    d->setTextColor(THEME_CYAN, THEME_BG);
    d->setCursor(8, 88); d->println("OSCILLOSCOPE / PULSE TRAIN:");

    if (SubGhzEngine::hasCapture()) {
        SubGhzSignal* sig = SubGhzEngine::getCapture();
        int drawX = 8, highY = 110, baselineY = 170;
        bool level = true;
        for (uint16_t i = 0; i < sig->count && drawX < SCREEN_WIDTH - 12; i++) {
            int width = sig->pulses[i] / 40; if (width < 2) width = 2; if (width > 30) width = 30;
            int currY = level ? highY : baselineY;
            d->drawFastHLine(drawX, currY, width, level ? THEME_GREEN : THEME_GREY);
            d->drawFastVLine(drawX, highY, baselineY - highY, THEME_DIM_CYAN);
            drawX += width; level = !level;
        }
        d->setTextColor(THEME_GREEN, THEME_BG);
        d->setCursor(8, 180); d->printf("Samples: %u @ 433.92 MHz", sig->count);
    } else {
        d->setTextColor(THEME_GREY, THEME_BG);
        d->setCursor(14, 150); d->println("No pulse train captured.");
        d->setCursor(14, 164); d->println("Hold a 433MHz remote near");
        d->setCursor(14, 178); d->println("the antenna + press SHUTTER.");
    }

    if (statusLine[0]) { d->setTextColor(statusColor, THEME_BG); d->setCursor(8, 244); d->print(statusLine); }

    DisplayUI::drawBottomBar("SNAP:Sniff RIGHT:Emu CLICK:Tx MENU:Exit");
    DisplayUI::endFrame();
}

// ============================================================
// 7. INFRARED TRANSCEIVER
// ============================================================
static void runIrMode(InputEvent evt) {
    static decode_results results;
    static bool hasDecodedSignal = false;

    if (evt == EVT_MENU || evt == EVT_BACK) { switchMode(MODE_MAIN_MENU); return; }

    bool decodedNow = false;
    if (evt == EVT_SHUTTER) {
        IREngine::blastUniversalPower();
    } else if (evt == EVT_CLICK && hasDecodedSignal) {
        if (results.decode_type == NEC)          IREngine::sendNEC(results.value, results.bits);
        else if (results.decode_type == SONY)    IREngine::sendSony(results.value, results.bits);
        else if (results.decode_type == RC5)     IREngine::sendRC5(results.value, results.bits);
        else if (results.decode_type == RC6)     IREngine::sendRC6(results.value, results.bits);
        else if (results.decode_type == SAMSUNG) IREngine::sendSamsung(results.value, results.bits);
    }

    if (IREngine::decode(&results)) {
        hasDecodedSignal = true; decodedNow = true;
        static uint32_t irCounter = 0;
        char fname[32]; snprintf(fname, sizeof(fname), "IR_%04u.ir", (unsigned)irCounter++);
        IREngine::saveToSD(&results, fname);
    }
    (void)decodedNow;

    DisplayUI::beginFrame();
    auto* d = DisplayUI::canvas();
    DisplayUI::drawTopBar("INFRARED TRANSCEIVER", THEME_AMBER, g_state.batteryPercent);

    d->setTextColor(THEME_CYAN, THEME_BG);
    d->setCursor(8, 28); d->println("TSOP RX GPIO45 | 940nm TX GPIO44");

    if (hasDecodedSignal) {
        d->setTextColor(THEME_GREEN, THEME_BG);
        d->setCursor(8, 50); d->printf("Protocol: %s", typeToString(results.decode_type).c_str());
        d->setCursor(8, 64); d->printf("Bits    : %u", results.bits);
        d->setCursor(8, 78); d->printf("Value   : 0x%llX", (unsigned long long)results.value);
        d->setCursor(8, 92); d->printf("Raw Len : %u", results.rawlen);
        d->setTextColor(THEME_CYAN, THEME_BG);
        d->setCursor(8, 112); d->println("CLICK to replay this code.");
    } else {
        d->setTextColor(THEME_WHITE, THEME_BG);
        d->setCursor(8, 54); d->println("- Point a remote to clone.");
        d->setCursor(8, 68); d->println("- SHUTTER: TV-B-Gone blast.");
        d->setCursor(8, 82); d->println("- CLICK: replay last code.");
    }

    DisplayUI::drawBottomBar("SNAP:TV-B-Gone CLICK:Replay MENU:Exit");
    DisplayUI::endFrame();

    if (evt == EVT_SHUTTER) triggerChirp(1500, 15);
    if (decodedNow) triggerChirp(1800, 10);
}

// ============================================================
// 8. SETTINGS (scrollable, functional, sectioned)
// ============================================================
enum SetId {
    SID_HDR_IDENTITY, SID_CALLSIGN, SID_STARTMODE, SID_PROFILE,
    SID_HDR_AESTHETICS, SID_THEME, SID_LAYOUT,
    SID_HDR_DISPLAY, SID_BRIGHT, SID_SOUND,
    SID_HDR_CAMERA, SID_SCENE, SID_STILLRES, SID_STILLQ, SID_CBRI, SID_CCON, SID_CSAT, SID_CSHARP, SID_CDEN, SID_CEV, SID_CGAIN, SID_CWB, SID_PREVRES, SID_VFLIP, SID_HMIR, SID_GRID,
    SID_HDR_NAMING, SID_PREFIX, SID_AUTOINDEX, SID_PROMPTNAME, SID_FORMAT,
    SID_HDR_RADIO, SID_HOP,
    SID_HDR_STORAGE, SID_BENCH, SID_HEALTH, SID_ERASE, SID_FILES,
    SID_HDR_CONFIG, SID_CONFIG_SAVE, SID_CONFIG_LOAD,
    SID_HDR_SYSTEM, SID_TELEMETRY, SID_REBOOT, SID_ABOUT, SID_RESET, SID_SAVE,
    SID_TOTAL
};

static const char* stillResNames[] = { "VGA", "HD", "UXGA 2MP", "QXGA 3MP" };
static const char* sceneNamesS[]   = { "AUTO", "INDOOR", "OUTDOOR", "NIGHT", "MACRO" };
static const char* wbNamesS[]      = { "AUTO", "SUNNY", "CLOUDY", "OFFICE", "HOME" };
static const char* gainNamesS[]    = { "2X", "4X", "8X", "16X", "32X", "64X", "128X" };
static const char* startModeNames[] = { "MAIN MENU", "CAMERA", "GALLERY", "EXPLORER", "NOTEPAD", "GAMES", "2.4G NRF", "WIFI 802.11", "BLE SCAN", "DRONE RADAR", "FOX HUNT", "SKIMMER", "SUB-GHZ", "INFRARED" };
static const int STARTMODE_COUNT = sizeof(startModeNames) / sizeof(startModeNames[0]);
static const char* themeNames[] = { "CYBERPUNK", "STEALTH", "FIELD AMBER" };
static const char* layoutNames[] = { "DASHBOARD", "2x3 GRID", "LIST STACK" };
static const char* profileNames[] = { "STANDARD", "STEALTH OPS", "SNIFFER LAB" };

static void runSettingsMode(InputEvent evt) {
    static int selIndex = SID_BRIGHT;
    static int scroll = 0;
    static float benchKbps = 0.0f;
    static int cardHealthy = -1; // -1 unknown, 0 fail, 1 ok
    static bool confirmErase = false, confirmYes = false;
    static bool confirmReboot = false, confirmRebootYes = false;

    const int VISIBLE = 11;
    CameraSettings& cam = SettingsManager::camera();

    // Build display rows
    static char names[SID_TOTAL][28];
    static char values[SID_TOTAL][18];
    static const char* namePtrs[SID_TOTAL];
    static const char* valPtrs[SID_TOTAL];
    static bool isHeader[SID_TOTAL];

    auto setRow = [&](int id, const char* n, const char* v, bool hdr) {
        strncpy(names[id], n, 27); names[id][27] = '\0';
        strncpy(values[id], v ? v : "", 17); values[id][17] = '\0';
        isHeader[id] = hdr;
    };
    char tmp[18];

    setRow(SID_HDR_IDENTITY, "OPERATOR IDENTITY", "", true);
    setRow(SID_CALLSIGN, "Callsign", SettingsManager::getOperatorCallsign(), false);
    uint8_t sm = SettingsManager::getStartupMode();
    setRow(SID_STARTMODE, "Boot Mode", sm < STARTMODE_COUNT ? startModeNames[sm] : "MAIN MENU", false);
    uint8_t pf = SettingsManager::getProfileMode();
    setRow(SID_PROFILE, "Active Profile", profileNames[pf % 3], false);

    setRow(SID_HDR_AESTHETICS, "THEME & UI DESIGN", "", true);
    uint8_t th = SettingsManager::getThemePreset();
    setRow(SID_THEME, "Color Theme", themeNames[th % 3], false);
    uint8_t ly = SettingsManager::getMenuLayout();
    setRow(SID_LAYOUT, "Home Layout", layoutNames[ly % 3], false);

    setRow(SID_HDR_DISPLAY, "DISPLAY & AUDIO", "", true);
    snprintf(tmp, sizeof(tmp), "%d%%", SettingsManager::getBrightness()); setRow(SID_BRIGHT, "Brightness (SW)", tmp, false);
    setRow(SID_SOUND, "Sound", SettingsManager::isSoundEnabled() ? "ON" : "MUTED", false);

    setRow(SID_HDR_CAMERA, "CAMERA & IMAGE", "", true);
    setRow(SID_SCENE, "Scene Preset", sceneNamesS[cam.scenePreset % 5], false);
    setRow(SID_STILLRES, "Still Resolution", stillResNames[cam.stillResIdx % 4], false);
    snprintf(tmp, sizeof(tmp), "Q%d", cam.stillQuality); setRow(SID_STILLQ, "Still Quality", tmp, false);
    snprintf(tmp, sizeof(tmp), "%+d", cam.brightness); setRow(SID_CBRI, "Brightness", tmp, false);
    snprintf(tmp, sizeof(tmp), "%+d", cam.contrast);   setRow(SID_CCON, "Contrast", tmp, false);
    snprintf(tmp, sizeof(tmp), "%+d", cam.saturation); setRow(SID_CSAT, "Saturation", tmp, false);
    snprintf(tmp, sizeof(tmp), "%d", cam.sharpness);   setRow(SID_CSHARP, "Sharpness", tmp, false);
    snprintf(tmp, sizeof(tmp), "%d", cam.denoise);     setRow(SID_CDEN, "Denoise", tmp, false);
    snprintf(tmp, sizeof(tmp), "%+d", cam.aeLevel);    setRow(SID_CEV, "Exposure EV", tmp, false);
    setRow(SID_CGAIN, "Gain Ceiling", gainNamesS[cam.gainCeiling % 7], false);
    setRow(SID_CWB, "White Balance", wbNamesS[cam.wbMode % 5], false);
    setRow(SID_PREVRES, "Preview Res", cam.previewResIdx ? "VGA" : "QVGA", false);
    setRow(SID_VFLIP, "V-Flip", cam.flipV ? "ON" : "OFF", false);
    setRow(SID_HMIR, "H-Mirror", cam.mirrorH ? "ON" : "OFF", false);
    setRow(SID_GRID, "Grid Overlay", gridNamesS[cam.gridMode % 4], false);

    setRow(SID_HDR_NAMING, "IMAGE NAMING & FORMAT", "", true);
    setRow(SID_PREFIX, "Prefix Base", SettingsManager::getImagePrefix(), false);
    snprintf(tmp, sizeof(tmp), "#%u", (unsigned)SettingsManager::getNextImageIndex());
    setRow(SID_AUTOINDEX, "Next Index", tmp, false);
    setRow(SID_PROMPTNAME, "Prompt on Snap", SettingsManager::isPromptNamingEnabled() ? "ON" : "OFF", false);
    setRow(SID_FORMAT, "Image Format", "JPG (LOCKED)", false);

    setRow(SID_HDR_RADIO, "RADIO", "", true);
    snprintf(tmp, sizeof(tmp), "%ds", SettingsManager::getChannelHopDelay()); setRow(SID_HOP, "Wi-Fi Ch Hop", tmp, false);

    setRow(SID_HDR_STORAGE, "STORAGE", "", true);
    if (benchKbps > 0) { snprintf(tmp, sizeof(tmp), "%.0fKB/s", benchKbps); setRow(SID_BENCH, "SD Benchmark", tmp, false); }
    else setRow(SID_BENCH, "SD Benchmark", "RUN >", false);
    setRow(SID_HEALTH, "SD Health Test", cardHealthy < 0 ? "RUN >" : (cardHealthy ? "PASS" : "FAIL"), false);
    setRow(SID_ERASE, "Erase Captures", "RUN >", false);
    setRow(SID_FILES, "File Explorer", "OPEN >", false);

    setRow(SID_HDR_CONFIG, "SMART CONFIG (.INI)", "", true);
    setRow(SID_CONFIG_SAVE, "Export Config", "SYNC >", false);
    setRow(SID_CONFIG_LOAD, "Reload INI File", "LOAD >", false);

    setRow(SID_HDR_SYSTEM, "SYSTEM & HARDWARE", "", true);
    float tC = 42.0f;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    tC = temperatureRead();
#endif
    snprintf(tmp, sizeof(tmp), "%.1fC / %uK", tC, (unsigned)(ESP.getFreePsram()/1024));
    setRow(SID_TELEMETRY, "Core Telemetry", tmp, false);
    setRow(SID_REBOOT, "Reboot Device", "EXEC >", false);
    setRow(SID_ABOUT, "About / Info", "OPEN >", false);
    setRow(SID_RESET, "Reset Defaults", "RUN >", false);
    setRow(SID_SAVE, "Save & Exit", "ENTER >", false);

    for (int i = 0; i < SID_TOTAL; i++) { namePtrs[i] = names[i]; valPtrs[i] = values[i]; }

    auto nextSel = [&](int dir) {
        int i = selIndex;
        do { i = (i + dir + SID_TOTAL) % SID_TOTAL; } while (isHeader[i]);
        selIndex = i;
    };

    // Confirm dialog for erase
    if (confirmErase) {
        if (evt == EVT_LEFT || evt == EVT_RIGHT) confirmYes = !confirmYes;
        else if (evt == EVT_CLICK) {
            if (confirmYes) { uint32_t n = SDManager::eraseCaptures(); Serial.printf("[SD] erased %u\n", (unsigned)n); refreshMedia(); triggerChirp(2200, 20); }
            confirmErase = false;
        } else if (evt == EVT_MENU || evt == EVT_BACK) confirmErase = false;

        DisplayUI::beginFrame();
        DisplayUI::drawSettingsMenu(selIndex, namePtrs, valPtrs, isHeader, SID_TOTAL, scroll, VISIBLE);
        DisplayUI::drawConfirm("ERASE CAPTURES", "Delete all media & logs?", confirmYes);
        DisplayUI::endFrame();
        return;
    }

    // Confirm dialog for reboot
    if (confirmReboot) {
        if (evt == EVT_LEFT || evt == EVT_RIGHT) confirmRebootYes = !confirmRebootYes;
        else if (evt == EVT_CLICK) {
            if (confirmRebootYes) {
                triggerChirp(2600, 40);
                SDManager::flush();
                SD_MMC.end();
                delay(120);
                esp_restart();
            }
            confirmReboot = false;
        } else if (evt == EVT_MENU || evt == EVT_BACK) confirmReboot = false;

        DisplayUI::beginFrame();
        DisplayUI::drawSettingsMenu(selIndex, namePtrs, valPtrs, isHeader, SID_TOTAL, scroll, VISIBLE);
        DisplayUI::drawConfirm("REBOOT DEVICE", "Restart Axion ESP32-S3?", confirmRebootYes);
        DisplayUI::endFrame();
        return;
    }

    if (evt == EVT_MENU || evt == EVT_BACK) { SettingsManager::save(); switchMode(MODE_MAIN_MENU); return; }

    if (evt == EVT_UP)   { nextSel(-1); triggerChirp(1400, 6); }
    if (evt == EVT_DOWN) { nextSel(+1); triggerChirp(1400, 6); }

    int dir = (evt == EVT_RIGHT) ? 1 : (evt == EVT_LEFT ? -1 : 0);
    if (dir != 0) {
        triggerChirp(1500, 6);
        switch (selIndex) {
            case SID_STARTMODE: {
                uint8_t cur = SettingsManager::getStartupMode();
                SettingsManager::setStartupMode((cur + dir + STARTMODE_COUNT) % STARTMODE_COUNT);
                break;
            }
            case SID_PROFILE: {
                int p = (int)SettingsManager::getProfileMode() + dir;
                if (p < 0) p = 2; else if (p > 2) p = 0;
                SettingsManager::setProfileMode((uint8_t)p);
                break;
            }
            case SID_THEME: {
                int t = (int)SettingsManager::getThemePreset() + dir;
                if (t < 0) t = 2; else if (t > 2) t = 0;
                SettingsManager::setThemePreset((uint8_t)t);
                break;
            }
            case SID_LAYOUT: {
                int l = (int)SettingsManager::getMenuLayout() + dir;
                if (l < 0) l = 2; else if (l > 2) l = 0;
                SettingsManager::setMenuLayout((uint8_t)l);
                break;
            }
            case SID_BRIGHT: SettingsManager::setBrightness(SettingsManager::getBrightness() + dir * 10); break;
            case SID_SOUND:  SettingsManager::setSoundEnabled(!SettingsManager::isSoundEnabled()); break;
            case SID_SCENE:  cam.scenePreset = (cam.scenePreset + dir + 5) % 5; break;
            case SID_STILLRES: cam.stillResIdx = (cam.stillResIdx + dir + 4) % 4; break;
            case SID_STILLQ: cam.stillQuality = constrain(cam.stillQuality + dir, 4, 20); break;
            case SID_CBRI:   cam.brightness = constrain(cam.brightness + dir, -2, 2); break;
            case SID_CCON:   cam.contrast = constrain(cam.contrast + dir, -2, 2); break;
            case SID_CSAT:   cam.saturation = constrain(cam.saturation + dir, -2, 2); break;
            case SID_CSHARP: cam.sharpness = constrain(cam.sharpness + dir, -3, 3); break;
            case SID_CDEN:   cam.denoise = constrain(cam.denoise + dir, 0, 8); break;
            case SID_CEV:    cam.aeLevel = constrain(cam.aeLevel + dir, -2, 2); break;
            case SID_CGAIN:  cam.gainCeiling = (cam.gainCeiling + dir + 7) % 7; break;
            case SID_CWB:    cam.wbMode = (cam.wbMode + dir + 5) % 5; break;
            case SID_PREVRES: cam.previewResIdx = cam.previewResIdx ? 0 : 1; break;
            case SID_VFLIP:  cam.flipV = !cam.flipV; break;
            case SID_HMIR:   cam.mirrorH = !cam.mirrorH; break;
            case SID_GRID:   cam.gridMode = (cam.gridMode + dir + 4) % 4; break;
            case SID_PREFIX: SettingsManager::cycleImagePrefix(); break;
            case SID_AUTOINDEX: {
                uint32_t curIdx = SettingsManager::getNextImageIndex();
                if (dir > 0) SettingsManager::incrementImageIndex();
                else if (curIdx > 1) { SettingsManager::setImagePrefix(SettingsManager::getImagePrefix()); }
                break;
            }
            case SID_PROMPTNAME: SettingsManager::setPromptNamingEnabled(!SettingsManager::isPromptNamingEnabled()); break;
            case SID_HOP:    SettingsManager::setChannelHopDelay(constrain((int)SettingsManager::getChannelHopDelay() + dir, 1, 10)); break;
            default: break;
        }
    }

    if (evt == EVT_CLICK) {
        switch (selIndex) {
            case SID_STARTMODE: {
                uint8_t cur = SettingsManager::getStartupMode();
                SettingsManager::setStartupMode((cur + 1) % STARTMODE_COUNT);
                triggerChirp(1800, 8);
                break;
            }
            case SID_PROFILE: {
                uint8_t p = (SettingsManager::getProfileMode() + 1) % 3;
                SettingsManager::setProfileMode(p);
                triggerChirp(1800, 8);
                break;
            }
            case SID_THEME: {
                uint8_t t = (SettingsManager::getThemePreset() + 1) % 3;
                SettingsManager::setThemePreset(t);
                triggerChirp(1800, 8);
                break;
            }
            case SID_LAYOUT: {
                uint8_t l = (SettingsManager::getMenuLayout() + 1) % 3;
                SettingsManager::setMenuLayout(l);
                triggerChirp(1800, 8);
                break;
            }
            case SID_SOUND: SettingsManager::setSoundEnabled(!SettingsManager::isSoundEnabled()); triggerChirp(1800, 8); break;
            case SID_PREFIX: SettingsManager::cycleImagePrefix(); triggerChirp(1800, 8); break;
            case SID_PROMPTNAME: SettingsManager::setPromptNamingEnabled(!SettingsManager::isPromptNamingEnabled()); triggerChirp(1800, 8); break;
            case SID_BENCH: triggerChirp(2000, 15); benchKbps = SDManager::benchmarkWriteSpeed(); triggerChirp(2400, 20); break;
            case SID_HEALTH: cardHealthy = SDManager::testCardHealth() ? 1 : 0; triggerChirp(cardHealthy ? 2200 : 800, 20); break;
            case SID_ERASE: confirmErase = true; confirmYes = false; triggerChirp(1800, 10); break;
            case SID_FILES: SettingsManager::save(); switchMode(MODE_FILE_EXPLORER); return;
            case SID_CONFIG_SAVE: {
                SettingsManager::saveToConfigFile(PATH_CONFIG_INI);
                triggerChirp(2400, 25);
                break;
            }
            case SID_CONFIG_LOAD: {
                bool ok = SettingsManager::loadFromConfigFile(PATH_CONFIG_INI);
                triggerChirp(ok ? 2200 : 800, ok ? 25 : 40);
                break;
            }
            case SID_REBOOT: {
                confirmReboot = true;
                confirmRebootYes = false;
                triggerChirp(1800, 10);
                break;
            }
            case SID_ABOUT: switchMode(MODE_ABOUT); return;
            case SID_RESET: SettingsManager::resetDefaults(); triggerChirp(2000, 20); break;
            case SID_SAVE:  SettingsManager::save(); triggerChirp(2000, 15); switchMode(MODE_MAIN_MENU); return;
            default: break;
        }
    }

    // Keep selection visible: compute scroll in terms of rows (headers included)
    if (selIndex < scroll) scroll = selIndex;
    if (selIndex >= scroll + VISIBLE) scroll = selIndex - VISIBLE + 1;
    if (scroll < 0) scroll = 0;
    if (scroll > SID_TOTAL - VISIBLE) scroll = SID_TOTAL - VISIBLE;

    DisplayUI::beginFrame();
    DisplayUI::drawSettingsMenu(selIndex, namePtrs, valPtrs, isHeader, SID_TOTAL, scroll, VISIBLE);
    DisplayUI::endFrame();
}

// ============================================================
// 9. ABOUT / SYSTEM INFO
// ============================================================
static void runAboutMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK || evt == EVT_CLICK) { switchMode(MODE_SETTINGS); return; }

    static char l[14][40];
    const char* lines[14];
    int n = 0;

    snprintf(l[n], 40, "#FIRMWARE"); lines[n] = l[n]; n++;
    snprintf(l[n], 40, "%s v%s", FW_NAME, FW_VERSION); lines[n] = l[n]; n++;
    snprintf(l[n], 40, "Build: %s", FW_BUILD); lines[n] = l[n]; n++;

    snprintf(l[n], 40, "#HARDWARE"); lines[n] = l[n]; n++;
    snprintf(l[n], 40, "SoC: ESP32-S3 @ %dMHz", (int)(ESP.getCpuFreqMHz())); lines[n] = l[n]; n++;
    snprintf(l[n], 40, "Flash: %dMB", (int)(ESP.getFlashChipSize() / (1024 * 1024))); lines[n] = l[n]; n++;
    size_t psTot = ESP.getPsramSize(); size_t psFree = ESP.getFreePsram();
    snprintf(l[n], 40, "PSRAM: %u/%uKB free", (unsigned)(psFree / 1024), (unsigned)(psTot / 1024)); lines[n] = l[n]; n++;
    snprintf(l[n], 40, "Heap: %uKB free", (unsigned)(ESP.getFreeHeap() / 1024)); lines[n] = l[n]; n++;

    snprintf(l[n], 40, "#STORAGE & POWER"); lines[n] = l[n]; n++;
    if (g_state.sdMounted)
        snprintf(l[n], 40, "SD: %s %lluMB", SDManager::getCardTypeName(), (unsigned long long)SDManager::getTotalSpaceMB());
    else snprintf(l[n], 40, "SD: NOT MOUNTED");
    lines[n] = l[n]; n++;
    snprintf(l[n], 40, "Battery: %.2fV (%d%%)", g_state.batteryVoltage, g_state.batteryPercent); lines[n] = l[n]; n++;
    uint32_t up = g_state.uptimeMs / 1000;
    snprintf(l[n], 40, "Uptime: %02u:%02u:%02u", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60)); lines[n] = l[n]; n++;

    int storagePct = g_state.sdMounted ? SDManager::getUsedPercent() : -1;

    DisplayUI::beginFrame();
    DisplayUI::drawInfoScreen("ABOUT // SYSTEM", THEME_CYAN, lines, n, storagePct);
    DisplayUI::endFrame();
    vTaskDelay(pdMS_TO_TICKS(120));
}

// ============================================================
// 10. DEVICE FEATURE GUIDE & USER MANUAL
// ============================================================
struct FeatureGuidePage {
    const char* category;
    uint16_t accent;
    const char* overview;
    const char* controls[5];
    int controlCount;
    const char* notes[4];
    int noteCount;
};

static const FeatureGuidePage GUIDE_PAGES[] = {
    {
        "CAMERA & VIDEO",
        THEME_CYAN,
        "Live 20+ FPS preview with hardware JPEG|OV3660 sensor supporting up to 3MP stills.",
        {
            "SHUTTER: Quick click saves full JPEG photo",
            "SHUTTER HOLD: Record AVI video to SD card",
            "UP / DOWN: Digital zoom & crop control",
            "CLICK (CENTER): Toggle quick settings menu",
            "MENU / BACK: Return to Main Menu"
        },
        5,
        {
            "Files save automatically to /DCIM on SD",
            "Quick menu adjusts scene, exposure & white bal",
            "Instant preview freeze confirms successful capture",
            "Fast 1-bit native SDMMC write pipeline"
        },
        4
    },
    {
        "GALLERY & MEDIA",
        THEME_WHITE,
        "Playback photos and recorded video files directly|from the SD card /DCIM directory.",
        {
            "UP / DOWN: Scroll through captured media",
            "LEFT / RIGHT: Previous or next photo",
            "CLICK: Play / Pause recorded AVI video",
            "CLICK HOLD: Delete selected media file",
            "MENU / BACK: Exit gallery"
        },
        5,
        {
            "Video plays back at smooth recorded FPS",
            "Full JPEG decode with hardware color scaling",
            "Delete feature includes confirmation guard",
            "File counter indicates position and total"
        },
        4
    },
    {
        "SD FILE EXPLORER",
        THEME_GREEN,
        "Hierarchical file browser for navigating folders,|inspecting file sizes and managing files.",
        {
            "UP / DOWN: Navigate file and directory list",
            "CLICK: Open file or enter directory",
            "LEFT: Navigate up to parent folder",
            "CLICK HOLD: Delete currently highlighted file",
            "MENU / BACK: Return to main menu"
        },
        5,
        {
            "Supports FAT32 and exFAT formatted cards",
            "Real-time free space bar shown at bottom",
            "Auto-detects images, logs, captures and audio",
            "Safe directory recursion prevent stack overflow"
        },
        4
    },
    {
        "2.4GHz RF ANALYZER",
        THEME_AMBER,
        "Sweeps 126 channels in the 2.4GHz spectrum using|an onboard or external NRF transceiver.",
        {
            "UP / DOWN: Adjust vertical peak scale",
            "LEFT / RIGHT: Select active frequency channel",
            "CLICK: Toggle peak hold and live waterfall",
            "SHUTTER: Reset spectrum sweep history",
            "MENU / BACK: Exit RF analyzer"
        },
        5,
        {
            "Displays 2400MHz to 2525MHz channels 0-125",
            "Real-time waterfall scroll reveals signal duty",
            "Detects Wi-Fi, BLE, and proprietary transmitters",
            "High sweep rate with low-latency SPI bus"
        },
        4
    },
    {
        "WI-FI 802.11 MONITOR",
        THEME_GREEN,
        "Promiscuous packet sniffer, frame analyzer and|network encryption detection utility.",
        {
            "UP / DOWN: Select network or channel",
            "CLICK: Toggle packet capture to PCAP on SD",
            "LEFT / RIGHT: Step channel hop delay",
            "SHUTTER: Trigger beacon or deauth transmission",
            "MENU / BACK: Exit Wi-Fi monitor"
        },
        5,
        {
            "Real-time beacon, data, deauth packet counts",
            "Parses RSN/WPA IEs for WPA, WPA2, WPA3, Open",
            "PCAP files can be opened in Wireshark",
            "Uses ESP32-S3 Core 0 for background RX"
        },
        4
    },
    {
        "BLE SCANNER",
        THEME_CRIMSON,
        "Bluetooth Low Energy discovery instrument with|Manufacturer ID and company resolution.",
        {
            "UP / DOWN: Scroll through discovered devices",
            "CLICK: View detailed manufacturer advertisement",
            "SHUTTER: Toggle proximity beacon test",
            "LEFT / RIGHT: Filter by RSSI signal strength",
            "MENU / BACK: Exit BLE scanner"
        },
        5,
        {
            "Identifies Apple, Google, Samsung, Microsoft",
            "Parses RSSI, MAC address and Service UUIDs",
            "Logs discovered devices to /LOGS on SD card",
            "Low-power background scanning engine"
        },
        4
    },
    {
        "SUB-GHZ 433MHz",
        THEME_AMBER,
        "Raw pulse analyzer for 433.92MHz ASK/OOK signals|with pulse duration and code decoding.",
        {
            "CLICK: Start listening / freeze pulse buffer",
            "SHUTTER: Transmit / replay recorded pulse train",
            "UP / DOWN: Zoom timing waveform display",
            "LEFT / RIGHT: Scroll captured pulse buffer",
            "MENU / BACK: Exit Sub-GHz engine"
        },
        5,
        {
            "Decodes EV1527, PT2262, and fixed remote codes",
            "Stores raw timing samples for exact replay",
            "Hardware pin switching safely isolates buzzer",
            "High resolution microsecond pulse measurement"
        },
        4
    },
    {
        "INFRARED ENGINE",
        THEME_CYAN,
        "38kHz TSOP receiver and 940nm blaster for learning|and testing infrared remote control protocols.",
        {
            "CLICK: Arm TSOP receiver for signal learning",
            "SHUTTER: Transmit selected infrared code",
            "UP / DOWN: Select protocol or stored preset",
            "LEFT / RIGHT: Select device brand (TV/Audio)",
            "MENU / BACK: Exit infrared engine"
        },
        5,
        {
            "Decodes NEC, Sony, RC5, and raw pulse trains",
            "Preloaded with universal power code library",
            "High-drive 940nm LED emitter output",
            "Saves captured codes to SD for later replay"
        },
        4
    },
    {
        "SYSTEM & SETTINGS",
        THEME_WHITE,
        "Global configuration for display brightness, audio|feedback, camera tuning and SD diagnostics.",
        {
            "UP / DOWN: Navigate settings list",
            "LEFT / RIGHT: Adjust values or toggle options",
            "CLICK: Execute action items (Benchmark, Reset)",
            "SHUTTER: Save settings immediately to SD",
            "MENU / BACK: Save configuration and exit"
        },
        5,
        {
            "Settings persist automatically to config file",
            "SD Benchmark tests raw card write bandwidth",
            "Camera options tune ISO, EV, WB, and grid",
            "Battery meter tracks real cell voltage"
        },
        4
    }
};
static const int GUIDE_PAGE_COUNT = sizeof(GUIDE_PAGES) / sizeof(GUIDE_PAGES[0]);
static int s_guidePage = 0;

static void runHelpMode(InputEvent evt) {
    if (evt == EVT_MENU || evt == EVT_BACK) {
        switchMode(MODE_MAIN_MENU);
        return;
    }

    if (evt == EVT_RIGHT || evt == EVT_CLICK) {
        s_guidePage = (s_guidePage + 1) % GUIDE_PAGE_COUNT;
        triggerChirp(1500, 8);
    } else if (evt == EVT_LEFT) {
        s_guidePage = (s_guidePage - 1 + GUIDE_PAGE_COUNT) % GUIDE_PAGE_COUNT;
        triggerChirp(1500, 8);
    }

    const FeatureGuidePage& p = GUIDE_PAGES[s_guidePage];

    DisplayUI::beginFrame();
    DisplayUI::drawGuideScreen(
        p.category, p.accent, s_guidePage, GUIDE_PAGE_COUNT,
        p.overview, p.controls, p.controlCount,
        p.notes, p.noteCount
    );
    DisplayUI::endFrame();
}

// ============================================================
// BOOTSTRAP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] AxionTool Cyber Multi-Tool Starting...");

    audioQueue = xQueueCreate(16, sizeof(AudioChirp));

    SPISemaphore::init();
    DisplayUI::init();
    InputController::init();
    AudioManager::init();

    DisplayUI::drawCyberpunkBootSplash();
    triggerChirp(2000, 15);

    if (SDManager::init()) {
        g_state.sdMounted = true;
        SettingsManager::init();
        Serial.println("[BOOT] SDMMC mounted & settings loaded");
    } else {
        SettingsManager::init(); // defaults in RAM
        Serial.println("[BOOT] SD Card Not Detected (running with defaults)");
    }

    xTaskCreatePinnedToCore(taskCore0, "Core0_Radio", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(taskAudio, "Core1_Audio", 2048, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(taskCore1, "Core1_UIApp", 10240, nullptr, 1, nullptr, 1);

    uint8_t bootMode = SettingsManager::getStartupMode();
    switch (bootMode) {
        case 1: switchMode(MODE_CAMERA); break;
        case 2: switchMode(MODE_GALLERY); break;
        case 3: switchMode(MODE_FILE_EXPLORER); break;
        case 4: switchMode(MODE_NOTEPAD); break;
        case 5: switchMode(MODE_GAMES); break;
        case 6: switchMode(MODE_NRF_SPECTRUM); break;
        case 7: switchMode(MODE_WIFI_ANALYZER); break;
        case 8: switchMode(MODE_BLE_ANALYZER); break;
        case 9: switchMode(MODE_REMOTE_ID); break;
        case 10: switchMode(MODE_FOX_HUNT); break;
        case 11: switchMode(MODE_SKIMMER_DETECTOR); break;
        case 12: switchMode(MODE_SUBGHZ); break;
        case 13: switchMode(MODE_IR); break;
        default:
            g_state.mode = MODE_MAIN_MENU;
            menuSelection = 0;
            break;
    }
    Serial.printf("[BOOT] System Ready (Mode: %u, Callsign: %s)\n", (unsigned)g_state.mode, SettingsManager::getOperatorCallsign());
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
