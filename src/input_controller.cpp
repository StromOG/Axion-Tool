#include "input_controller.h"
#include "pin_config.h"
#include "config.h"
#include <Arduino.h>

uint32_t InputController::lastJoyPollMs = 0;
uint32_t InputController::lastShutterMs = 0;
uint32_t InputController::lastMenuMs    = 0;
uint32_t InputController::lastClickMs   = 0;

bool InputController::lastShutterRaw = true;
bool InputController::lastMenuRaw    = false;
bool InputController::lastClickRaw   = true;
uint32_t InputController::shutterPressStart = 0;
bool InputController::shutterHeld    = false;

uint32_t InputController::touchPressStart = 0;
uint32_t InputController::lastTapReleaseMs = 0;
bool InputController::touchHoldFired = false;
uint32_t InputController::totalTouchTaps = 0;

float InputController::smoothX = (float)JOY_CENTER;
float InputController::smoothY = (float)JOY_CENTER;

void InputController::init() {
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
    pinMode(PIN_BTN_SHUTTER, INPUT_PULLUP);
    pinMode(PIN_BTN_MENU, INPUT_PULLDOWN); // TTP223 Active HIGH

    // 12-Bit ADC1 configuration (Wi-Fi and BLE safe)
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
}

void InputController::processJoystick(InputEvent& evt) {
    uint32_t now = millis();
    if (now - lastJoyPollMs < 20) return; // 50 Hz polling rate
    lastJoyPollMs = now;

    int rawX = analogRead(PIN_JOY_X);
    int rawY = analogRead(PIN_JOY_Y);

    // Exponential Moving Average (EMA) smoothing filter
    smoothX = smoothX * 0.7f + (float)rawX * 0.3f;
    smoothY = smoothY * 0.7f + (float)rawY * 0.3f;

    static uint32_t lastNavRepeat = 0;
    if (now - lastNavRepeat < 160) return; // Prevent runaway cursor speed

    float deltaX = smoothX - JOY_CENTER;
    float deltaY = smoothY - JOY_CENTER;

    if (abs(deltaX) > JOY_DEADBAND || abs(deltaY) > JOY_DEADBAND) {
        if (abs(deltaX) > abs(deltaY)) {
            evt = deltaX > 0 ? EVT_RIGHT : EVT_LEFT;
        } else {
            evt = deltaY > 0 ? EVT_DOWN : EVT_UP;
        }
        lastNavRepeat = now;
    }
}

void InputController::getNormalizedJoystick(float& outX, float& outY) {
    outX = (smoothX - JOY_CENTER) / 2048.0f;
    outY = (smoothY - JOY_CENTER) / 2048.0f;
    if (abs(outX) < (float)JOY_DEADBAND / 2048.0f) outX = 0.0f;
    if (abs(outY) < (float)JOY_DEADBAND / 2048.0f) outY = 0.0f;
}

bool InputController::checkClick() {
    bool state = digitalRead(PIN_JOY_SW);
    uint32_t now = millis();
    if (state == LOW && lastClickRaw == HIGH && (now - lastClickMs) > 180) {
        lastClickMs = now;
        lastClickRaw = state;
        return true;
    }
    lastClickRaw = state;
    return false;
}

bool InputController::checkShutter() {
    return false; // Handled dynamically in poll() for tap vs hold
}

bool InputController::isTouchPressed() {
    return digitalRead(PIN_BTN_MENU) == HIGH;
}

uint32_t InputController::getTouchHoldDuration() {
    if (digitalRead(PIN_BTN_MENU) == HIGH && touchPressStart > 0) {
        return millis() - touchPressStart;
    }
    return 0;
}

uint32_t InputController::getTouchTapCount() {
    return totalTouchTaps;
}

InputEvent InputController::processCapacitiveTouch() {
    uint32_t now = millis();
    bool touchState = digitalRead(PIN_BTN_MENU); // TTP223 Active HIGH on GPIO 46

    if (touchState == HIGH) {
        if (lastMenuRaw == LOW) {
            // Touch down edge
            touchPressStart = now;
            touchHoldFired = false;
        } else if (!touchHoldFired && (now - touchPressStart >= 550)) {
            // Touch held continuously > 550ms: Long Hold Action
            touchHoldFired = true;
            lastMenuRaw = touchState;
            return EVT_TOUCH_HOLD;
        }
    } else {
        if (lastMenuRaw == HIGH) {
            // Touch released edge
            uint32_t touchDur = now - touchPressStart;
            lastMenuRaw = touchState;
            if (!touchHoldFired && touchDur >= 30 && touchDur < 550) {
                totalTouchTaps++;
                if (now - lastTapReleaseMs < 380) {
                    // Double Tap detected!
                    lastTapReleaseMs = 0;
                    return EVT_TOUCH_DOUBLE;
                } else {
                    lastTapReleaseMs = now;
                    return EVT_MENU; // Clean Single Tap (Back / Exit / Cycle layout)
                }
            }
        }
    }
    lastMenuRaw = touchState;
    return EVT_NONE;
}

InputEvent InputController::poll() {
    uint32_t now = millis();
    bool shutterState = digitalRead(PIN_BTN_SHUTTER);

    // Shutter state machine: Tap vs Hold
    if (shutterState == LOW) { // Button is pressed down
        if (lastShutterRaw == HIGH) { // Just pressed
            shutterPressStart = now;
            shutterHeld = false;
        } else if (!shutterHeld && (now - shutterPressStart >= 650)) { // Held > 650ms
            shutterHeld = true;
            lastShutterRaw = shutterState;
            return EVT_SHUTTER_HOLD;
        }
    } else { // Button is released
        if (lastShutterRaw == LOW) {
            uint32_t pressDur = now - shutterPressStart;
            lastShutterRaw = shutterState;
            if (!shutterHeld && pressDur >= 40 && pressDur < 650) {
                return EVT_SHUTTER; // Crisp short tap
            }
        }
    }
    lastShutterRaw = shutterState;

    // Check capacitive touch multi-gesture engine
    InputEvent touchEvt = processCapacitiveTouch();
    if (touchEvt != EVT_NONE) return touchEvt;

    if (checkClick()) return EVT_CLICK;

    InputEvent evt = EVT_NONE;
    processJoystick(evt);
    return evt;
}

float InputController::getBatteryVoltage() {
    // 100k / 100k divider on ADC1 GPIO 1 (VRx tapped line)
    int raw = analogRead(PIN_BATTERY_ADC);
    float pinVolts = ((float)raw / 4095.0f) * 3.3f;
    return pinVolts * 2.0f; // Multiplied by 2 for divider ratio
}

uint8_t InputController::getBatteryPercent() {
    float v = getBatteryVoltage();
    if (v <= 3.40f) return 0;
    if (v >= 4.20f) return 100;
    return (uint8_t)(((v - 3.40f) / (4.20f - 3.40f)) * 100.0f);
}
