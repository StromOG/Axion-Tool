#include "sd_manager.h"
#include "pin_config.h"
#include "config.h"
#include <Arduino.h>

bool SDManager::mounted = false;

bool SDManager::init() {
    SD_MMC.setPins(PIN_SDMMC_CLK, PIN_SDMMC_CMD, PIN_SDMMC_D0);

    // 1-bit SDMMC with 40MHz high-speed bus clock for high-res JPEG streaming
    if (!SD_MMC.begin("/sdcard", true, false, 40000)) {
        // Fallback to default clock speed if card negotiation fails
        if (!SD_MMC.begin("/sdcard", true, false)) {
            Serial.println("[SD] Mount failed");
            mounted = false;
            return false;
        }
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No card inserted");
        mounted = false;
        return false;
    }

    Serial.printf("[SD] Card mounted: %llu MB total / %llu MB free\n",
                  SD_MMC.cardSize() / (1024ULL * 1024ULL),
                  (SD_MMC.cardSize() - SD_MMC.usedBytes()) / (1024ULL * 1024ULL));

    mounted = true;
    createDirectories();
    return true;
}

bool SDManager::isMounted() { return mounted; }

void SDManager::createDirectories() {
    if (!mounted) return;
    const char* dirs[] = { PATH_DCIM, PATH_PCAP, PATH_SUBGHZ, PATH_IR };
    for (auto d : dirs) {
        if (!SD_MMC.exists(d)) {
            SD_MMC.mkdir(d);
        }
    }
}

bool SDManager::writeFile(const char* path, const uint8_t* data, size_t len) {
    if (!mounted) return false;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;
    size_t w = f.write(data, len);
    f.close();
    return w == len;
}

bool SDManager::appendFile(const char* path, const uint8_t* data, size_t len) {
    if (!mounted) return false;
    File f = SD_MMC.open(path, FILE_APPEND);
    if (!f) return false;
    size_t w = f.write(data, len);
    f.close();
    return w == len;
}

size_t SDManager::readFile(const char* path, uint8_t* buffer, size_t maxLen) {
    if (!mounted) return 0;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return 0;
    size_t r = f.read(buffer, maxLen);
    f.close();
    return r;
}

bool SDManager::deleteFile(const char* path) {
    if (!mounted) return false;
    return SD_MMC.remove(path);
}

uint64_t SDManager::getFreeSpaceMB() {
    if (!mounted) return 0;
    uint64_t total = SD_MMC.cardSize();
    uint64_t used  = SD_MMC.usedBytes();
    return (total - used) / (1024ULL * 1024ULL);
}

uint64_t SDManager::getTotalSpaceMB() {
    if (!mounted) return 0;
    return SD_MMC.cardSize() / (1024ULL * 1024ULL);
}

uint8_t SDManager::getUsedPercent() {
    if (!mounted) return 0;
    uint64_t total = SD_MMC.cardSize();
    if (total == 0) return 0;
    return (uint8_t)((SD_MMC.usedBytes() * 100ULL) / total);
}

const char* SDManager::getCardTypeName() {
    if (!mounted) return "NONE";
    switch (SD_MMC.cardType()) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        default:        return "UNKNOWN";
    }
}

uint32_t SDManager::countFiles(const char* dirPath) {
    if (!mounted) return 0;
    File dir = SD_MMC.open(dirPath);
    if (!dir) return 0;

    uint32_t count = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) count++;
        entry = dir.openNextFile();
    }
    dir.close();
    return count;
}

float SDManager::benchmarkWriteSpeed() {
    if (!mounted) return 0.0f;
    const size_t chunkSize = 4096;
    const size_t totalBytes = 256 * 1024; // 256 KB test block
    uint8_t* testBuf = (uint8_t*)ps_malloc(chunkSize);
    if (!testBuf) testBuf = (uint8_t*)malloc(chunkSize);
    if (!testBuf) return 0.0f;

    memset(testBuf, 0xAA, chunkSize);
    const char* benchPath = "/bench.tmp";

    uint32_t startMs = millis();
    File f = SD_MMC.open(benchPath, FILE_WRITE);
    if (!f) {
        free(testBuf);
        return 0.0f;
    }

    size_t written = 0;
    while (written < totalBytes) {
        size_t w = f.write(testBuf, chunkSize);
        if (w == 0) break;
        written += w;
    }
    f.flush();
    f.close();
    uint32_t elapsedMs = millis() - startMs;
    free(testBuf);
    SD_MMC.remove(benchPath);

    if (elapsedMs == 0) elapsedMs = 1;
    float kbps = ((float)written / 1024.0f) / ((float)elapsedMs / 1000.0f);
    return kbps;
}

bool SDManager::testCardHealth() {
    if (!mounted) return false;
    const char* testFile = "/health.tmp";
    File f = SD_MMC.open(testFile, FILE_WRITE);
    if (!f) return false;

    uint8_t sample[64];
    for (int i = 0; i < 64; i++) sample[i] = (uint8_t)(i ^ 0x5A);
    f.write(sample, sizeof(sample));
    f.close();

    f = SD_MMC.open(testFile, FILE_READ);
    if (!f) {
        SD_MMC.remove(testFile);
        return false;
    }

    uint8_t readback[64] = {0};
    size_t r = f.read(readback, sizeof(readback));
    f.close();
    SD_MMC.remove(testFile);

    return (r == sizeof(sample) && memcmp(sample, readback, sizeof(sample)) == 0);
}

static int sdEntryCompare(const SdEntry& a, const SdEntry& b) {
    if (a.isDir != b.isDir) return a.isDir ? -1 : 1;  // directories first
    return strcasecmp(a.name, b.name);
}

size_t SDManager::listDir(const char* path, SdEntry* out, size_t maxEntries) {
    if (!mounted || !out || maxEntries == 0) return 0;
    File dir = SD_MMC.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    size_t count = 0;
    File entry = dir.openNextFile();
    while (entry && count < maxEntries) {
        const char* full = entry.name();
        const char* base = strrchr(full, '/');
        base = base ? base + 1 : full;
        if (base[0] != '\0' && base[0] != '.') {
            strncpy(out[count].name, base, sizeof(out[count].name) - 1);
            out[count].name[sizeof(out[count].name) - 1] = '\0';
            out[count].size = entry.isDirectory() ? 0 : entry.size();
            out[count].isDir = entry.isDirectory();
            count++;
        }
        entry = dir.openNextFile();
    }
    dir.close();

    // Simple insertion sort (directories first, then by name)
    for (size_t i = 1; i < count; i++) {
        SdEntry key = out[i];
        int j = (int)i - 1;
        while (j >= 0 && sdEntryCompare(out[j], key) > 0) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

uint64_t SDManager::fileSize(const char* path) {
    if (!mounted) return 0;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return 0;
    uint64_t sz = f.size();
    f.close();
    return sz;
}

uint32_t SDManager::eraseFolderContents(const char* dirPath) {
    if (!mounted) return 0;
    File dir = SD_MMC.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    uint32_t removed = 0;
    char rmPath[160];
    File entry = dir.openNextFile();
    while (entry) {
        bool isDir = entry.isDirectory();
        // name() may return a basename or a full path depending on core version;
        // always rebuild an absolute path from dirPath + basename for removal.
        const char* full = entry.name();
        const char* base = strrchr(full, '/');
        base = base ? base + 1 : full;
        if (!isDir && base[0] != '\0') {
            snprintf(rmPath, sizeof(rmPath), "%s/%s", dirPath, base);
            entry = dir.openNextFile();   // advance before removing
            if (SD_MMC.remove(rmPath)) removed++;
        } else {
            entry = dir.openNextFile();
        }
    }
    dir.close();
    Serial.printf("[SD] Erased %u files from %s\n", (unsigned)removed, dirPath);
    return removed;
}

uint32_t SDManager::eraseCaptures() {
    uint32_t n = 0;
    n += eraseFolderContents(PATH_DCIM);
    n += eraseFolderContents(PATH_PCAP);
    n += eraseFolderContents(PATH_SUBGHZ);
    n += eraseFolderContents(PATH_IR);
    createDirectories();
    return n;
}

uint32_t SDManager::deleteAllPhotos() {
    uint32_t n = eraseFolderContents(PATH_DCIM);
    createDirectories();
    return n;
}
