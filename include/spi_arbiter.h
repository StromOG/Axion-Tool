#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Arduino.h>

class SPISemaphore {
public:
    static void init();
    static bool acquire(uint32_t timeout_ms = 25);
    static void release();
    static void lock();
    static void unlock();

private:
    static SemaphoreHandle_t spiMutex;
};

// RAII helper for atomic bus access
class SPIScopedLock {
public:
    SPIScopedLock(uint32_t timeout_ms = 25) {
        locked = SPISemaphore::acquire(timeout_ms);
    }
    ~SPIScopedLock() {
        if (locked) SPISemaphore::release();
    }
    bool isLocked() const { return locked; }

private:
    bool locked;
};
