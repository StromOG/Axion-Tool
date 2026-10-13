#pragma once
#include <RF24.h>
#include <SPI.h>

class NRFScanner {
public:
    static bool init();
    static void deinit();
    static void startSweep();
    static void stopSweep();
    static bool sweepStep();
    static uint8_t getCurrentChannel();
    static int8_t getCurrentRSSI();
    static void getSpectrum(int8_t* out, uint8_t count);
    static bool readPacket(uint8_t* buffer, uint8_t* len);

    // Waterfall buffer management in PSRAM
    static void initWaterfallBuffer(uint8_t width, uint8_t height);
    static void updateWaterfall();
    static uint8_t* getWaterfallBuffer();
    static uint8_t getWaterfallWidth();
    static uint8_t getWaterfallHeight();
};

SPIClass& nrfSPI();
