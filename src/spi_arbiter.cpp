#include "spi_arbiter.h"

SemaphoreHandle_t SPISemaphore::spiMutex = nullptr;

void SPISemaphore::init() {
    if (spiMutex == nullptr) {
        spiMutex = xSemaphoreCreateMutex();
    }
}

bool SPISemaphore::acquire(uint32_t timeout_ms) {
    if (spiMutex == nullptr) return false;
    return xSemaphoreTake(spiMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void SPISemaphore::release() {
    if (spiMutex != nullptr) {
        xSemaphoreGive(spiMutex);
    }
}

void SPISemaphore::lock()   { acquire(portMAX_DELAY); }
void SPISemaphore::unlock() { release(); }
