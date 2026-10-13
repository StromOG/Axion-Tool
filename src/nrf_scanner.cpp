#include "nrf_scanner.h"
#include "spi_arbiter.h"
#include "pin_config.h"
#include <SPI.h>

static SPIClass hspi(HSPI);

SPIClass& nrfSPI() { return hspi; }

static RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
static bool scanning = false;
static uint8_t currentChannel = 0;
static int8_t lastRSSI = -100;
static int8_t spectrum[126];

// PSRAM-allocated waterfall matrix
static uint8_t* waterfallMatrix = nullptr;
static uint8_t wfWidth = 126;
static uint8_t wfHeight = 60;

bool NRFScanner::init() {
    SPIScopedLock lock(100);
    if (!lock.isLocked()) return false;

    hspi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_NRF_CSN);

    bool ok = radio.begin(&hspi);
    if (ok) {
        radio.setAutoAck(false);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_1MBPS);
        radio.setCRCLength(RF24_CRC_DISABLED);
        radio.setPayloadSize(32);
        radio.startListening();
        radio.stopListening();
        for (int i = 0; i < 126; i++) spectrum[i] = -100;
        initWaterfallBuffer(126, 60);
    }
    return ok;
}

void NRFScanner::deinit() {
    SPIScopedLock lock(100);
    if (lock.isLocked()) {
        radio.powerDown();
    }
    scanning = false;
}

void NRFScanner::startSweep() { scanning = true; }
void NRFScanner::stopSweep()  { scanning = false; }

bool NRFScanner::sweepStep() {
    if (!scanning) return false;
    SPIScopedLock lock(8);
    if (!lock.isLocked()) return false;

    radio.setChannel(currentChannel);
    radio.startListening();
    vTaskDelay(pdMS_TO_TICKS(1));
    bool carrier = radio.testCarrier();
    bool hasData = radio.available();
    int8_t rssi = -100;
    if (carrier || hasData) {
        rssi = -60 + (carrier ? 20 : 0);
    }
    radio.stopListening();

    lastRSSI = rssi;
    spectrum[currentChannel] = rssi;

    currentChannel = (currentChannel + 1) % 126;
    return true;
}

uint8_t NRFScanner::getCurrentChannel() { return currentChannel; }
int8_t  NRFScanner::getCurrentRSSI()    { return lastRSSI; }

void NRFScanner::getSpectrum(int8_t* out, uint8_t count) {
    uint8_t n = count < 126 ? count : 126;
    memcpy(out, spectrum, n);
}

bool NRFScanner::readPacket(uint8_t* buffer, uint8_t* len) {
    SPIScopedLock lock(8);
    if (!lock.isLocked()) return false;
    bool ok = false;
    if (radio.available()) {
        uint8_t n = radio.getPayloadSize();
        radio.read(buffer, n);
        *len = n;
        ok = true;
    }
    return ok;
}

void NRFScanner::initWaterfallBuffer(uint8_t width, uint8_t height) {
    wfWidth = width;
    wfHeight = height;
    if (!waterfallMatrix) {
        // TRD 3.2: Allocate heavy arrays in PSRAM
        waterfallMatrix = (uint8_t*)ps_malloc(width * height);
        if (waterfallMatrix) {
            memset(waterfallMatrix, 0, width * height);
        }
    }
}

void NRFScanner::updateWaterfall() {
    if (!waterfallMatrix) return;
    // Shift rows up by 1
    memmove(waterfallMatrix, waterfallMatrix + wfWidth, wfWidth * (wfHeight - 1));
    // Fill bottom row with latest spectrum intensity
    for (uint8_t x = 0; x < wfWidth; x++) {
        int8_t v = spectrum[(x * 126) / wfWidth];
        uint8_t intensity = (v + 100) * 2; // Map -100..0 to 0..200
        waterfallMatrix[(wfHeight - 1) * wfWidth + x] = intensity;
    }
}

uint8_t* NRFScanner::getWaterfallBuffer() { return waterfallMatrix; }
uint8_t NRFScanner::getWaterfallWidth()  { return wfWidth; }
uint8_t NRFScanner::getWaterfallHeight() { return wfHeight; }
