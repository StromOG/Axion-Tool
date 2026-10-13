#pragma once
#include <stdint.h>

#define SUBGHZ_MAX_PULSES   512
#define SUBGHZ_MIN_PULSE_US 100
#define SUBGHZ_MAX_PULSE_US 10000

struct SubGhzSignal {
    uint32_t pulses[SUBGHZ_MAX_PULSES];
    uint16_t count;
    uint32_t frequency;
    char name[32];
};

class SubGhzEngine {
public:
    static void init();
    static void deinit();

    // RX — raw pulse capture via RMT
    static void startCapture();
    static void stopCapture();
    static bool isCapturing();
    static bool hasCapture();
    static SubGhzSignal* getCapture();

    // TX — replay captured signal
    static void transmit(SubGhzSignal* sig);

    // Demodulation & Synthesis (PT2262, EV1527)
    static bool decodeFixedCode(SubGhzSignal* sig, uint32_t* address, uint8_t* data);
    static bool decodeEV1527(SubGhzSignal* sig, uint32_t* serial, uint8_t* button);
    static void synthesizeEV1527(uint32_t serial, uint8_t button, SubGhzSignal* out);

    // Storage
    static bool saveToSD(SubGhzSignal* sig, const char* filename);
    static bool loadFromSD(const char* filename, SubGhzSignal* out);
};
