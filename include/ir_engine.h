#pragma once
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

class IREngine {
public:
    static void init();
    static void deinit();

    // RX Handlers
    static void startReceive();
    static void stopReceive();
    static bool isReceiving();
    static bool decode(decode_results* results);

    // TX Blasters & Jamming
    static void sendNEC(uint64_t data, uint16_t nbits);
    static void sendSony(uint64_t data, uint16_t nbits);
    static void sendRC5(uint64_t data, uint16_t nbits);
    static void sendRC6(uint64_t data, uint16_t nbits);
    static void sendSamsung(uint64_t data, uint16_t nbits);
    static void sendRaw(const uint16_t* pulses, uint16_t len, uint16_t khz = 38);
    static void blastUniversalPower(); // TV-B-Gone rapid sequence across all major brands

    // SD Database storage (.ir files)
    static bool saveToSD(decode_results* results, const char* filename);
    static bool loadFromSD(const char* filename, decode_results* out);
};
