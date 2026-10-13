#pragma once
#include <stdint.h>
#include "system_state.h"

class InputController {
public:
    static void init();
    static InputEvent poll();

    // Smoothed Joystick Coordinates with Deadband
    static void getNormalizedJoystick(float& outX, float& outY);

    // Battery Voltage & Percent Estimation
    static float getBatteryVoltage();
    static uint8_t getBatteryPercent();

private:
    static void processJoystick(InputEvent& evt);
    static bool checkClick();
    static bool checkShutter();
    static bool checkMenu();

    static uint32_t lastJoyPollMs;
    static uint32_t lastShutterMs;
    static uint32_t lastMenuMs;
    static uint32_t lastClickMs;

    static bool lastShutterRaw;
    static bool lastMenuRaw;
    static bool lastClickRaw;
    static uint32_t shutterPressStart;
    static bool shutterHeld;

    static float smoothX;
    static float smoothY;
};
