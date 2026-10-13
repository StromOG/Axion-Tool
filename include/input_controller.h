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

    // Capacitive Touch Diagnostics & State (TTP223 on GPIO 46)
    static bool isTouchPressed();
    static uint32_t getTouchHoldDuration();
    static uint32_t getTouchTapCount();

private:
    static void processJoystick(InputEvent& evt);
    static bool checkClick();
    static bool checkShutter();
    static InputEvent processCapacitiveTouch();

    static uint32_t lastJoyPollMs;
    static uint32_t lastShutterMs;
    static uint32_t lastMenuMs;
    static uint32_t lastClickMs;

    static bool lastShutterRaw;
    static bool lastMenuRaw;
    static bool lastClickRaw;
    static uint32_t shutterPressStart;
    static bool shutterHeld;

    // TTP223 Capacitive Touch Multi-Gesture Engine State
    static uint32_t touchPressStart;
    static uint32_t lastTapReleaseMs;
    static bool touchHoldFired;
    static uint32_t totalTouchTaps;

    static float smoothX;
    static float smoothY;
};
