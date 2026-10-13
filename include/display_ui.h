#pragma once
#include <LovyanGFX.hpp>
#include "spi_arbiter.h"
#include "icon_assets.h"
#include "sd_manager.h"

// ==========================================
// SPACE GALAXY & MONOCHROME PALETTE (RGB565)
// ==========================================
#define THEME_BG          0x0000  // Cosmic Void / OLED Deep Black (#000000)
#define THEME_PANEL       0x0847  // Deep Interstellar Space (#070B16)
#define THEME_SURFACE     0x10A9  // Cosmic Dust Surface (#111728)
#define THEME_CYAN        0x07FF  // Electric Starlight Cyan (#00F0FF)
#define THEME_NEBULA_PURP 0x995F  // Radiant Nebula Violet (#9B51E0)
#define THEME_STELLAR_BLU 0x33BD  // Deep Celestial Blue (#3377FF)
#define THEME_STAR_WHITE  0xFFFF  // Pure Starlight Supernova (#FFFFFF)
#define THEME_GREEN       0x37E0  // Auroral Green (#39FF14)
#define THEME_AMBER       0xFD20  // Solar Corona Gold (#FFB000)
#define THEME_CRIMSON     0xF800  // Red Supergiant (#FF003C)
#define THEME_WHITE       0xFFFF  // Pure Starlight White
#define THEME_GREY        0x7BEF  // Cosmic Dust Grey (#7B889B)
#define THEME_DIM_CYAN    0x0210  // Hologram Grid Accent (#073844)

// Premium Black & White Theme for Settings
#define THEME_BW_BG       0x0000  // Pure Black (#000000)
#define THEME_BW_SURFACE  0x1082  // Deep Graphite (#121212)
#define THEME_BW_CARD_SEL 0x2104  // Elevated Slate (#202020)
#define THEME_BW_BORDER   0x4A69  // Subtle Border (#4A4A4A)
#define THEME_BW_WHITE    0xFFFF  // Crisp White (#FFFFFF)
#define THEME_BW_SILVER   0xCE59  // Silver Accent (#CCCCCC)
#define THEME_BW_MUTED    0x8410  // Midtone Muted (#808080)

struct MenuItem {
    const char* label;
    const char* subtext;
    uint8_t iconId;
    uint16_t accentColor;
};

class DisplayUI {
public:
    static void init();
    static LGFX* get();                 // raw panel (direct draw, e.g. camera)
    static lgfx::LGFXBase* canvas();    // current off-screen draw target
    static void clear(uint16_t color = THEME_BG);

    // Flicker-free buffered compose: beginFrame() clears the PSRAM back buffer,
    // draw with the helpers / canvas(), then endFrame() dims + pushes it once.
    static void beginFrame(uint16_t bg = THEME_BG);
    static void endFrame();

    // Smooth Animated Menu Renderer
    static void drawAnimatedMenu(const MenuItem* items, uint8_t count, int selectedIndex, float animOffset);

    // Modern Header & Footer HUD
    static void drawTopBar(const char* title, uint16_t accentColor, uint8_t batteryPercent = 100);
    static void drawBottomBar(const char* hint);
    static void drawCenteredText(const char* text, int y, uint16_t color, uint8_t size = 1);
    static void drawStatusBox(const char* title, const char* msg, uint16_t color);

    // Shared Axion Cyber Star Logo renderer (geometric star with faceted shading & orbital chevrons)
    static void drawAxionStarLogo(int cx, int cy, float size, float glowAlpha = 1.0f, bool drawChevrons = true);

    // Tactical Cyber Graffiti & Tag Accent Renderer (drawn on top of header/HUD)
    static void drawCyberGraffitiHUD(int x, int y, const char* tag = "AXION", uint16_t primaryCol = THEME_CYAN, uint16_t accentCol = THEME_AMBER);

    // Premium animated boot sequence (blocking, sprite-buffered ~2.2s)
    static void drawCyberpunkBootSplash();

    // Icon rendering helper
    static void drawIcon(int x, int y, uint8_t iconId, uint16_t fgColor, uint16_t bgColor = THEME_BG);

    // Viewfinder Overlays (drawn DIRECTLY over the live camera blit)
    static void drawCameraViewfinderHUD(float fps, uint64_t freeStorageMB, bool sdReady, float digitalZoom = 1.0f,
                                        const char* resName = "QVGA", const char* filterName = "NORMAL",
                                        int brightness = 0, int contrast = 0, uint8_t gridMode = 1,
                                        bool isRecording = false, bool isTimelapse = false,
                                        const char* wbName = "AUTO", int quickMenuIndex = -1,
                                        uint32_t recSec = 0, uint32_t recFrames = 0, const char* sceneName = "AUTO");

    static void drawCameraQuickMenu(int selectedIndex, const char* items[], int itemCount);
    static void drawCameraQuickValue(const char* label, const char* value);

    // Shutter Freeze & Instant Capture Preview (renders the just-saved JPEG)
    static void showCaptureFreezeFlash(const char* savedPath = nullptr);
    static void showVideoRecordAnimation(bool starting);

    // Photo / Video Gallery Playback Screen (buffered)
    static void drawGalleryScreen(const char* imgPath, int imgIndex, int totalImgs, bool fileValid, bool isVideo = false);
    static void drawVideoFrame(const uint8_t* jpg, size_t len, int frameIdx, int totalFrames, bool playing);

    // Interactive scrollable System Settings Menu Screen (buffered)
    static void drawSettingsMenu(int selectedIndex, const char* names[], const char* values[],
                                 const bool* isHeader, int itemCount, int scrollOffset, int visibleRows);

    // SD File Explorer (buffered)
    static void drawFileExplorer(const char* path, const SdEntry* entries, int count,
                                 int selectedIndex, int scrollOffset, uint64_t freeMB, uint64_t totalMB);

    // Generic info / About screen (buffered)
    static void drawInfoScreen(const char* title, uint16_t accent, const char* lines[], int count, int storagePct = -1);

    // Feature Guide & Documentation screen (buffered, scrollable)
    static void drawGuideScreen(const char* categoryTitle, uint16_t accent, int pageIdx, int totalPages,
                                const char* overview, const char* controls[], int controlCount,
                                const char* notes[], int noteCount);

    // Confirmation dialog (buffered)
    static void drawConfirm(const char* title, const char* msg, bool yesSelected);

    // Ultra-compact Bottom-Docked Virtual Keyboard for Text Input / File Naming
    static void drawNamingKeyboardDialog(const char* currentText, int keyRow, int keyCol,
                                         const char* fixedFormat = "JPG", const char* title = "NAME PHOTO",
                                         const char* hint = "JOY:Move  CLICK:Type  SHUTTER:Save");

    // Full-featured Notepad Editor (buffered)
    static void drawNotepadEditor(const char* fileName, const char* content, int keyRow, int keyCol,
                                  bool namingStep, const char* nameBuffer, uint32_t toastUntil = 0,
                                  bool exitPrompt = false, int promptChoice = 0);

    // Retro Mini-Games System (buffered)
    static void drawRetroGames(int gameId, int subState, int score, int highScore,
                               int p1X, int p1Y, int p2X, int p2Y, int extra1, int extra2,
                               const void* extraData = nullptr, int spaceSeed = 0);

    // Tactical Marauder Suites (buffered)
    static void drawTacticalDroneRadar(uint8_t droneCount, int selectedIdx, float radarAngle);
    static void drawTacticalFoxHunt(const char* name, const uint8_t mac[6], int8_t rssi, float filteredRssi,
                                    int8_t peakRssi, float estDistanceM, uint8_t channel, bool isLocked, float radarAngle);
    static void drawTacticalSkimmerScanner(uint8_t count, int selectedIdx, uint32_t scanMs);

    // Double-buffered rendering via PSRAM Sprites
    static LGFX_Sprite* createPsramSprite(int w, int h);


private:
    static void applyBrightness(uint8_t pct); // scanline dimming on the back buffer
};
