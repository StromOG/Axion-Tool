#pragma once
#include <stdint.h>

// ==========================================
// FIRMWARE IDENTITY
// ==========================================
#define FW_NAME         "AxionTool"
#define FW_VERSION      "2.5.0"
#define FW_BUILD        "ESP32-S3 N16R8"

// ==========================================
// UI COLOR PALETTE (RGB565)
// ==========================================
#define COLOR_BG        0x0000  // Pure Black
#define COLOR_PANEL     0x0861  // Slate Grey
#define COLOR_CYAN      0x07FF  // Neon Cyan
#define COLOR_GREEN     0x47E0  // Neon Green
#define COLOR_AMBER     0xFD20  // Warning Amber
#define COLOR_CRIMSON   0xF800  // Alert Crimson
#define COLOR_WHITE     0xFFFF
#define COLOR_GREY      0x7BEF

// ==========================================
// SYSTEM CONSTANTS
// ==========================================
#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   320
#define SPI_FREQ_HZ     40000000
#define JOY_DEADBAND    400
#define JOY_CENTER      2048
#define INPUT_POLL_MS   20
#define CAMERA_FPS_MS   45   // ~22 FPS target

// ==========================================
// STORAGE PATHS
// ==========================================
#define PATH_DCIM       "/DCIM"
#define PATH_PCAP       "/PCAP"
#define PATH_SUBGHZ     "/SUBGHZ"
#define PATH_IR         "/IR"
#define PATH_SETTINGS   "/settings.json"
#define PATH_CONFIG_INI "/axion_config.ini"
#define PATH_SYSTEM_DIR "/SYSTEM"
