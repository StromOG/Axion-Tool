#include "ir_engine.h"
#include "pin_config.h"
#include "sd_manager.h"
#include "config.h"
#include <Arduino.h>
#include <SD_MMC.h>

static IRrecv* irrecv = nullptr;
static IRsend* irsend = nullptr;
static decode_results irResults;
static bool receiving = false;

void IREngine::init() {
    if (!irrecv) {
        irrecv = new IRrecv(PIN_IR_RX, 1024, 15, true);
        irrecv->enableIRIn();
    }
    if (!irsend) {
        irsend = new IRsend(PIN_IR_TX);
        irsend->begin();
    }
    receiving = true;
}

void IREngine::deinit() {
    if (irrecv) {
        irrecv->disableIRIn();
        delete irrecv;
        irrecv = nullptr;
    }
    if (irsend) {
        delete irsend;
        irsend = nullptr;
    }
    receiving = false;
}

void IREngine::startReceive() {
    if (irrecv && !receiving) {
        irrecv->enableIRIn();
        receiving = true;
    }
}

void IREngine::stopReceive() {
    if (irrecv && receiving) {
        irrecv->disableIRIn();
        receiving = false;
    }
}

bool IREngine::isReceiving() {
    return receiving;
}

bool IREngine::decode(decode_results* results) {
    if (!irrecv) return false;
    if (irrecv->decode(&irResults)) {
        memcpy(results, &irResults, sizeof(decode_results));
        irrecv->resume();
        return true;
    }
    return false;
}

void IREngine::sendNEC(uint64_t data, uint16_t nbits)      { if (irsend) irsend->sendNEC(data, nbits); }
void IREngine::sendSony(uint64_t data, uint16_t nbits)     { if (irsend) irsend->sendSony(data, nbits); }
void IREngine::sendRC5(uint64_t data, uint16_t nbits)      { if (irsend) irsend->sendRC5(data, nbits); }
void IREngine::sendRC6(uint64_t data, uint16_t nbits)      { if (irsend) irsend->sendRC6(data, nbits); }
void IREngine::sendSamsung(uint64_t data, uint16_t nbits)  { if (irsend) irsend->sendSamsung(data, nbits); }

void IREngine::sendRaw(const uint16_t* pulses, uint16_t len, uint16_t khz) {
    if (irsend) irsend->sendRaw(pulses, len, khz);
}

// Universal TV Power Codes database (covers >90% of worldwide commercial TVs: Sony, Samsung, LG, Panasonic, Sharp, Toshiba, Philips, Vizio)
struct UniversalPowerCode {
    uint8_t protocol; // 0=NEC, 1=Sony, 2=RC5, 3=Samsung, 4=Panasonic
    uint64_t data;
    uint16_t bits;
};

static const UniversalPowerCode universalPowerCodes[] = {
    // Sony 12-bit Power (0xA90)
    { 1, 0xA90, 12 },
    { 1, 0xA90, 15 },
    // Samsung Power (0xE0E040BF, 32-bit)
    { 3, 0xE0E040BF, 32 },
    // LG / GoldStar NEC Power (0x20DF10EF, 32-bit)
    { 0, 0x20DF10EF, 32 },
    // Toshiba NEC Power (0x02FD48B7, 32-bit)
    { 0, 0x02FD48B7, 32 },
    // Sharp Power (0x41A2, 15-bit)
    { 0, 0x41A2, 15 },
    // Panasonic 48-bit (0x40040100BCBD)
    { 0, 0x40040100BCBDULL, 48 },
    // Philips RC5 Power (0x80C, 12-bit)
    { 2, 0x80C, 12 },
    { 2, 0x00C, 12 },
    // Vizio NEC Power (0x20DF10EF alternative / 0x00FD48B7)
    { 0, 0x00FD48B7, 32 },
    // TCL / Thomson NEC Power (0x00FF00FF)
    { 0, 0x00FF00FF, 32 },
    // Hisense NEC Power (0x00FDB04F)
    { 0, 0x00FDB04F, 32 },
    // Insignia NEC Power (0x00FD48B7)
    { 0, 0x01FE48B7, 32 },
    // Sanyo NEC Power (0x00FD7887)
    { 0, 0x00FD7887, 32 }
};

void IREngine::blastUniversalPower() {
    if (!irsend) return;

    const size_t totalCodes = sizeof(universalPowerCodes) / sizeof(universalPowerCodes[0]);
    for (size_t i = 0; i < totalCodes; i++) {
        const auto& c = universalPowerCodes[i];
        switch (c.protocol) {
            case 0: // NEC
                irsend->sendNEC(c.data, c.bits);
                break;
            case 1: // Sony
                irsend->sendSony(c.data, c.bits, 3); // Sony requires 3 repeats
                break;
            case 2: // RC5
                irsend->sendRC5(c.data, c.bits);
                break;
            case 3: // Samsung
                irsend->sendSamsung(c.data, c.bits);
                break;
            default:
                irsend->sendNEC(c.data, c.bits);
                break;
        }
        delay(25); // Inter-code settling delay
    }
}

bool IREngine::saveToSD(decode_results* results, const char* filename) {
    if (!SDManager::isMounted() || !results) return false;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", PATH_IR, filename);

    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;

    f.printf("Filetype: AxionTool IR Data\n");
    f.printf("Protocol: %s\n", typeToString(results->decode_type, results->repeat).c_str());
    f.printf("Bits: %u\n", results->bits);
    f.printf("Value: 0x%llX\n", (unsigned long long)results->value);
    f.printf("RawCount: %u\n", results->rawlen);
    f.printf("RAW: ");
    for (uint16_t i = 0; i < results->rawlen; i++) {
        f.printf("%u ", results->rawbuf[i] * RAWTICK);
    }
    f.printf("\n");
    f.close();
    return true;
}

bool IREngine::loadFromSD(const char* filename, decode_results* out) {
    if (!SDManager::isMounted() || !out) return false;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", PATH_IR, filename);

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.startsWith("Bits:"))  out->bits = line.substring(6).toInt();
        if (line.startsWith("Value:")) out->value = strtoull(line.substring(7).c_str(), nullptr, 16);
    }
    f.close();
    return true;
}
