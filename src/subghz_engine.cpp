#include "subghz_engine.h"
#include "pin_config.h"
#include "sd_manager.h"
#include "config.h"
#include "driver/rmt.h"
#include "freertos/ringbuf.h"
#include <Arduino.h>

static SubGhzSignal captureBuffer;
static volatile bool capturing = false;
static rmt_channel_t rxChannel = RMT_CHANNEL_2;
static rmt_channel_t txChannel = RMT_CHANNEL_3;
static RingbufHandle_t rxRingbuf = nullptr;

void SubGhzEngine::init() {
    rmt_config_t rx = {};
    rx.channel = rxChannel;
    rx.gpio_num = (gpio_num_t)PIN_SUBGHZ_RX;
    rx.clk_div = 80; // 1 MHz tick = 1µs
    rx.mem_block_num = 2;
    rx.rmt_mode = RMT_MODE_RX;
    rx.rx_config.filter_en = true;
    rx.rx_config.filter_ticks_thresh = 100;
    rx.rx_config.idle_threshold = 5000;
    rmt_config(&rx);
    rmt_driver_install(rx.channel, 2048, 0);

    rmt_config_t tx = {};
    tx.channel = txChannel;
    tx.gpio_num = (gpio_num_t)PIN_SUBGHZ_TX;
    tx.clk_div = 80;
    tx.mem_block_num = 2;
    tx.rmt_mode = RMT_MODE_TX;
    tx.tx_config.loop_en = false;
    tx.tx_config.carrier_en = false;
    tx.tx_config.idle_output_en = true;
    tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    rmt_config(&tx);
    rmt_driver_install(tx.channel, 0, 0);
}

void SubGhzEngine::deinit() {
    rmt_driver_uninstall(rxChannel);
    rmt_driver_uninstall(txChannel);
}

void SubGhzEngine::startCapture() {
    captureBuffer.count = 0;
    captureBuffer.frequency = 433920000;
    strncpy(captureBuffer.name, "CAPTURE", sizeof(captureBuffer.name));
    capturing = true;
    rmt_get_ringbuf_handle(rxChannel, &rxRingbuf);
    rmt_rx_start(rxChannel, true);
}

void SubGhzEngine::stopCapture() {
    capturing = false;

    // Drain the RMT RX ring buffer into the pulse-duration list. Each RMT item
    // carries two level/duration pairs; we store the raw microsecond durations
    // in alternating HIGH/LOW order for the demodulators and the replayer.
    if (rxRingbuf) {
        size_t rxSize = 0;
        rmt_item32_t* items = (rmt_item32_t*)xRingbufferReceive(rxRingbuf, &rxSize, pdMS_TO_TICKS(50));
        while (items) {
            size_t n = rxSize / sizeof(rmt_item32_t);
            for (size_t i = 0; i < n && captureBuffer.count < SUBGHZ_MAX_PULSES; i++) {
                if (items[i].duration0 == 0) break;  // end-of-frame marker
                captureBuffer.pulses[captureBuffer.count++] = items[i].duration0;
                if (captureBuffer.count >= SUBGHZ_MAX_PULSES) break;
                if (items[i].duration1 == 0) break;
                captureBuffer.pulses[captureBuffer.count++] = items[i].duration1;
            }
            vRingbufferReturnItem(rxRingbuf, (void*)items);
            items = (rmt_item32_t*)xRingbufferReceive(rxRingbuf, &rxSize, 0);
        }
    }
    rmt_rx_stop(rxChannel);
}

bool SubGhzEngine::isCapturing() { return capturing; }
bool SubGhzEngine::hasCapture()  { return captureBuffer.count > 0; }
SubGhzSignal* SubGhzEngine::getCapture() { return &captureBuffer; }

void SubGhzEngine::transmit(SubGhzSignal* sig) {
    if (!sig || sig->count == 0) return;

    // Pair the alternating HIGH/LOW durations back into RMT items (OOK/ASK).
    static rmt_item32_t items[SUBGHZ_MAX_PULSES / 2 + 1];
    uint16_t itemCount = 0;
    for (uint16_t i = 0; i < sig->count && itemCount < (SUBGHZ_MAX_PULSES / 2 + 1); i += 2) {
        items[itemCount].level0 = 1;
        items[itemCount].duration0 = sig->pulses[i] & 0x7FFF;
        items[itemCount].level1 = 0;
        items[itemCount].duration1 = (i + 1 < sig->count) ? (sig->pulses[i + 1] & 0x7FFF) : 0;
        itemCount++;
    }

    // Repeat the frame several times so fixed-code receivers latch reliably.
    for (int rep = 0; rep < 5; rep++) {
        rmt_write_items(txChannel, items, itemCount, true);
        rmt_wait_tx_done(txChannel, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}

bool SubGhzEngine::decodeEV1527(SubGhzSignal* sig, uint32_t* serial, uint8_t* button) {
    // Very basic EV1527 decoder — assumes standard 24-bit frame
    // Sync = ~310 high, then 24 bits of (1 = 3:1 duty, 0 = 1:3 duty)
    if (!sig || sig->count < 48) return false;

    uint32_t value = 0;
    for (int i = 0; i < 24; i++) {
        uint32_t high = sig->pulses[i * 2];
        uint32_t low  = sig->pulses[i * 2 + 1];
        uint32_t ratio = (high * 100) / (high + low);
        value = (value << 1) | (ratio > 60 ? 1 : 0);
    }
    *serial = value >> 4;
    *button = value & 0x0F;
    return true;
}

bool SubGhzEngine::decodeFixedCode(SubGhzSignal* sig, uint32_t* address, uint8_t* data) {
    if (!sig || sig->count < 48) return false;
    // PT2262 standard tri-state fixed code demodulator
    uint32_t addr = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t high = sig->pulses[i * 4];
        uint32_t low = sig->pulses[i * 4 + 1];
        if (high > low) addr = (addr << 1) | 1;
        else addr = (addr << 1);
    }
    *address = addr;
    *data = (uint8_t)(sig->pulses[40] > sig->pulses[41] ? 1 : 0);
    return true;
}

void SubGhzEngine::synthesizeEV1527(uint32_t serial, uint8_t button, SubGhzSignal* out) {
    if (!out) return;
    out->frequency = 433920000;
    out->count = 0;
    strncpy(out->name, "EV1527_EMU", sizeof(out->name));

    // Standard EV1527 timing: 1 tick ~= 350us
    // Preamble: 1 tick HIGH, 31 ticks LOW
    out->pulses[out->count++] = 350;
    out->pulses[out->count++] = 10850;

    // 20-bit serial + 4-bit button = 24-bit payload
    uint32_t payload = ((serial & 0xFFFFF) << 4) | (button & 0x0F);

    for (int i = 23; i >= 0 && out->count < SUBGHZ_MAX_PULSES - 2; i--) {
        bool bit = (payload >> i) & 1;
        if (bit) {
            // '1' = 3 ticks HIGH, 1 tick LOW
            out->pulses[out->count++] = 1050;
            out->pulses[out->count++] = 350;
        } else {
            // '0' = 1 tick HIGH, 3 ticks LOW
            out->pulses[out->count++] = 350;
            out->pulses[out->count++] = 1050;
        }
    }
}

bool SubGhzEngine::saveToSD(SubGhzSignal* sig, const char* filename) {
    if (!SDManager::isMounted() || !sig) return false;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", PATH_SUBGHZ, filename);

    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;

    f.printf("Filetype: AxionTool SubGHz\n");
    f.printf("Frequency: %u\n", sig->frequency);
    f.printf("Pulses: %u\n", sig->count);
    f.printf("RAW: ");
    for (uint16_t i = 0; i < sig->count; i++) {
        f.printf("%u ", sig->pulses[i]);
    }
    f.printf("\n");
    f.close();
    return true;
}

bool SubGhzEngine::loadFromSD(const char* filename, SubGhzSignal* out) {
    if (!SDManager::isMounted() || !out) return false;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", PATH_SUBGHZ, filename);

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return false;

    out->count = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.startsWith("Pulses:")) {
            out->count = line.substring(7).toInt();
        } else if (line.startsWith("RAW:")) {
            String raw = line.substring(4);
            char* tok = strtok(const_cast<char*>(raw.c_str()), " ");
            uint16_t i = 0;
            while (tok && i < out->count) {
                out->pulses[i++] = atoi(tok);
                tok = strtok(nullptr, " ");
            }
        }
    }
    f.close();
    return true;
}
