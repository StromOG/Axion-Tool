#pragma once

// ============================================================================
// ESP32-S3 N16R8 HARDWARE PIN MAP & GPIO BUDGET MATRIX
// ============================================================================
// Flash / Octal 8MB PSRAM : GPIO 26-37 (Dead/Internal OPI Bus)
// Native USB D- / D+       : GPIO 19, 20
// Total Usable GPIOs       : 14 Pins Only (GPIO 0,1,2,3,14,21,41,42,43,44,45,46,47,48)
// ============================================================================

// ==========================================
// 1. SHARED HARDWARE SPI BUS
// Master Clock, MOSI, MISO shared by ILI9341 & NRF24L01
// ==========================================
#define PIN_SPI_SCK         14      // Shared SPI SCK
#define PIN_SPI_MOSI        47      // Shared SPI MOSI
#define PIN_SPI_MISO        48      // Shared SPI MISO

// ==========================================
// 2. TFT DISPLAY (ILI9341 240x320 SPI)
// RST wired to hardware EN Pin; Backlight wired to 3.3V Rail
// ==========================================
#define PIN_TFT_CS          41      // Display Chip Select (active LOW)
#define PIN_TFT_DC          42      // Display Data / Command register select
#define PIN_TFT_RST         -1      // Tied to EN Pin (Hardware auto-reset)

// ==========================================
// 3. NRF24L01+PA+LNA 2.4GHz TRANSCEIVER
// ==========================================
#define PIN_NRF_CSN         21      // NRF24 Chip Select Not (active LOW)
#define PIN_NRF_CE          43      // NRF24 Transmit / Receive Strobe

// ==========================================
// 4. ANALOG & DIGITAL USER CONTROLS
// ==========================================
#define PIN_JOY_X           1       // ADC1_CH0 (Wi-Fi safe horizontal axis)
#define PIN_JOY_Y           2       // ADC1_CH1 (Wi-Fi safe vertical axis)
#define PIN_JOY_SW          3       // Thumbstick click switch (INPUT_PULLUP)
#define PIN_BATTERY_ADC     1       // ADC1_CH0 (100k/100k battery divider tapped on GPIO 1)
#define PIN_BTN_SHUTTER     0       // Top push button to GND (Capture / Action)
#define PIN_BTN_MENU        46      // TTP223 capacitive touch menu/back button

// ==========================================
// 5. AUDIO FEEDBACK (BUZZER)
// ==========================================
#define PIN_BUZZER          44      // LEDC PWM Passive Buzzer (50% duty cycle)

// ==========================================
// 6. INFRARED SYSTEM
// ==========================================
#define PIN_IR_RX           45      // TSOP 18180D 38kHz demodulated IR input
#define PIN_IR_TX           44      // 940nm Clear LED via 100Ω (modular, shared w/ buzzer & SubGHz RX)

// ==========================================
// 7. MODULAR EXPANSION HEADER (SUB-GHZ 433MHz)
// ==========================================
#define PIN_SUBGHZ_TX       43      // STX882 ASK Transmitter
#define PIN_SUBGHZ_RX       44      // SRX882 Superheterodyne Receiver

// ==========================================
// 8. ONBOARD SDMMC STORAGE (1-BIT MODE)
// ==========================================
#define PIN_SDMMC_CLK       38      // Native SDMMC Clock
#define PIN_SDMMC_CMD       39      // Native SDMMC Command
#define PIN_SDMMC_D0        40      // Native SDMMC Data 0

// ==========================================
// 9. OV3660 DVP CAMERA BUS (14 Dedicated Pins)
// ==========================================
#define PWDN_GPIO_NUM       -1
#define RESET_GPIO_NUM      -1
#define XCLK_GPIO_NUM       15
#define SIOD_GPIO_NUM       4
#define SIOC_GPIO_NUM       5

#define Y9_GPIO_NUM         16
#define Y8_GPIO_NUM         17
#define Y7_GPIO_NUM         18
#define Y6_GPIO_NUM         12
#define Y5_GPIO_NUM         10
#define Y4_GPIO_NUM         8
#define Y3_GPIO_NUM         9
#define Y2_GPIO_NUM         11

#define VSYNC_GPIO_NUM      6
#define HREF_GPIO_NUM       7
#define PCLK_GPIO_NUM       13
