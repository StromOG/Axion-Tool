#pragma once
#include <SD_MMC.h>
#include <FS.h>

struct SdEntry {
    char     name[64];
    uint64_t size;
    bool     isDir;
};

class SDManager {
public:
    static bool init();
    static bool isMounted();
    static void createDirectories();
    static bool writeFile(const char* path, const uint8_t* data, size_t len);
    static bool appendFile(const char* path, const uint8_t* data, size_t len);
    static size_t readFile(const char* path, uint8_t* buffer, size_t maxLen);
    static bool deleteFile(const char* path);
    static uint64_t getFreeSpaceMB();
    static uint64_t getTotalSpaceMB();
    static uint8_t  getUsedPercent();
    static const char* getCardTypeName();
    static float benchmarkWriteSpeed(); // Tests sustained write speed in KB/s
    static uint32_t countFiles(const char* dirPath);
    static bool testCardHealth();

    // Directory listing for the File Explorer / Gallery. Fills up to maxEntries,
    // directories first then files, each sorted by name. Returns entry count.
    static size_t listDir(const char* path, SdEntry* out, size_t maxEntries);
    static uint64_t fileSize(const char* path);

    // Safe bulk operations (NOT a raw FAT format)
    static uint32_t eraseFolderContents(const char* dirPath);
    static uint32_t eraseCaptures();   // clears DCIM/PCAP/SUBGHZ/IR contents
    static uint32_t deleteAllPhotos(); // clears DCIM only

private:
    static bool mounted;
};
