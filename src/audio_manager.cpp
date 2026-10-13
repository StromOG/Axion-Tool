#include "audio_manager.h"
#include "pin_config.h"
#include <Arduino.h>

bool AudioManager::enabled = true;

// Buzzer uses a dedicated LEDC channel/timer so it NEVER collides with the
// OV3660 XCLK generator, which the camera driver pins to LEDC channel 0 / timer 0.
#define BUZZER_LEDC_CHANNEL  2   // channel 2 -> timer 1 in the Arduino LEDC mapping

void AudioManager::init() {
    // GPIO 44 is the passive buzzer (shared modular pin with IR TX / SubGHz RX).
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

void AudioManager::beep(uint16_t freqHz, uint16_t durationMs) {
    if (!enabled) return;

    ledcSetup(BUZZER_LEDC_CHANNEL, freqHz, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
    ledcWriteTone(BUZZER_LEDC_CHANNEL, freqHz);

    vTaskDelay(pdMS_TO_TICKS(durationMs));

    ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
    ledcDetachPin(PIN_BUZZER);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);  // leave the shared pin idle-low for IR/SubGHz
}

void AudioManager::clickSound()   { beep(1600, 8); }
void AudioManager::shutterSound() { beep(2400, 15); vTaskDelay(pdMS_TO_TICKS(15)); beep(1800, 15); }
void AudioManager::navSound()     { beep(1100, 6); }
void AudioManager::errorSound()   { beep(350, 60); }
void AudioManager::radarPing()    { beep(2200, 10); }

void AudioManager::setEnabled(bool e) { enabled = e; }
bool AudioManager::isEnabled() { return enabled; }
