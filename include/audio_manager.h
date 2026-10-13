#pragma once
#include <stdint.h>

class AudioManager {
public:
    static void init();
    static void beep(uint16_t freqHz, uint16_t durationMs);
    static void clickSound();
    static void shutterSound();
    static void navSound();
    static void errorSound();
    static void radarPing();
    static void setEnabled(bool enabled);
    static bool isEnabled();

private:
    static bool enabled;
};
