#include "camera_manager.h"
#include "pin_config.h"
#include "sd_manager.h"
#include "settings_manager.h"
#include "config.h"
#include <Arduino.h>
#include <SD_MMC.h>

bool CameraManager::initialized = false;
framesize_t CameraManager::currentFrameSize = FRAMESIZE_QVGA; // 320x240 for 22+ FPS preview
framesize_t CameraManager::stillFrameSize = FRAMESIZE_QXGA;   // 2048x1536 (Full 3MP native sensor resolution)
uint8_t CameraManager::stillQuality = 6;                       // Low compression (best quality)
uint32_t CameraManager::imageCounter = 0;
CameraFilterMode CameraManager::currentFilter = FILTER_NORMAL;
int CameraManager::brightnessVal = 0;
int CameraManager::contrastVal = 1;
int CameraManager::saturationVal = 0;
int CameraManager::sharpnessVal = 2;
int CameraManager::denoiseVal = 4;
int CameraManager::exposureVal = 0;
int CameraManager::gainCeilingVal = 2; // GAINCEILING_8X
int CameraManager::wbVal = 0; // 0=Auto, 1=Sunny, 2=Cloudy, 3=Office, 4=Home
CameraScene CameraManager::sceneVal = SCENE_AUTO;
bool CameraManager::hmirror = false;
bool CameraManager::vflip = true;
float CameraManager::zoomLevel = 1.0f;
int CameraManager::panOffsetX = 0;
int CameraManager::panOffsetY = 0;
uint8_t CameraManager::gridMode = 1; // 1 = Cyber Reticle default

bool CameraManager::timelapseActive = false;
uint32_t CameraManager::timelapseIntervalSec = 3;
uint32_t CameraManager::lastTimelapseCaptureMs = 0;
uint32_t CameraManager::timelapseCount = 0;

bool CameraManager::recordingVideo = false;
File CameraManager::videoFile;
uint32_t CameraManager::videoFrameCount = 0;
uint32_t CameraManager::videoStartTimeMs = 0;
uint16_t CameraManager::videoWidth = 320;
uint16_t CameraManager::videoHeight = 240;
uint32_t CameraManager::videoBytesWritten = 0;

static const char* filterNames[] = {
    "NORMAL", "NEGATIVE", "GRAYSCALE", "RED MATRIX",
    "GREEN CYBER", "BLUE HIFI", "RETRO SEPIA", "SOLARIZE"
};

static const char* wbNames[] = { "AUTO", "SUNNY", "CLOUDY", "OFFICE", "HOME" };
static const char* sceneNames[] = { "AUTO", "INDOOR", "OUTDOOR", "NIGHT", "MACRO" };

// Map still resolution index 0..3 to framesize
static framesize_t stillIdxToFrameSize(int idx) {
    switch (idx) {
        case 0: return FRAMESIZE_VGA;
        case 1: return FRAMESIZE_HD;
        case 2: return FRAMESIZE_UXGA;
        default: return FRAMESIZE_QXGA;
    }
}
static int frameSizeToStillIdx(framesize_t fs) {
    switch (fs) {
        case FRAMESIZE_VGA:  return 0;
        case FRAMESIZE_HD:   return 1;
        case FRAMESIZE_UXGA: return 2;
        default:             return 3;
    }
}

bool CameraManager::init() {
    if (initialized) return true;

    // Pull persisted configuration first so init applies the user's saved look
    loadFromSettings();

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    // Allocate frame buffers strictly in 8MB Octal PSRAM (OPI)
    if (psramFound()) {
        config.frame_size   = currentFrameSize;
        config.jpeg_quality = 10;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        config.grab_mode    = CAMERA_GRAB_LATEST;
    } else {
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] OV3660 Init Error: 0x%x\n", err);
        initialized = false;
        return false;
    }

    initialized = true;

    // Apply all user/ISP settings to the freshly initialized sensor
    applyAllSettings();

    // Determine initial image counter from SD card DCIM folder
    if (SDManager::isMounted()) {
        File dir = SD_MMC.open(PATH_DCIM);
        if (dir) {
            uint32_t count = 0;
            File file = dir.openNextFile();
            while (file) {
                if (!file.isDirectory()) count++;
                file = dir.openNextFile();
            }
            imageCounter = count;
            dir.close();
        }
    }

    Serial.println("[CAM] OV3660 3MP Sensor Ready with PSRAM pipeline");
    return true;
}

void CameraManager::deinit() {
    if (!initialized) return;
    if (recordingVideo) stopVideoRecord();
    esp_camera_deinit();
    initialized = false;
    Serial.println("[CAM] Sensor Deinitialized");
}

bool CameraManager::isInitialized() { return initialized; }

void CameraManager::loadFromSettings() {
    CameraSettings& c = SettingsManager::camera();
    brightnessVal   = c.brightness;
    contrastVal     = c.contrast;
    saturationVal   = c.saturation;
    sharpnessVal    = c.sharpness;
    exposureVal     = c.aeLevel;
    denoiseVal      = c.denoise;
    gainCeilingVal  = c.gainCeiling;
    wbVal           = c.wbMode;
    stillQuality    = c.stillQuality;
    stillFrameSize  = stillIdxToFrameSize(c.stillResIdx);
    currentFrameSize = (c.previewResIdx == 1) ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
    gridMode        = c.gridMode;
    sceneVal        = (CameraScene)(c.scenePreset % SCENE_COUNT);
    vflip           = c.flipV;
    hmirror         = c.mirrorH;
}

void CameraManager::storeToSettings() {
    CameraSettings& c = SettingsManager::camera();
    c.brightness    = brightnessVal;
    c.contrast      = contrastVal;
    c.saturation    = saturationVal;
    c.sharpness     = sharpnessVal;
    c.aeLevel       = exposureVal;
    c.denoise       = denoiseVal;
    c.gainCeiling   = gainCeilingVal;
    c.wbMode        = wbVal;
    c.stillQuality  = stillQuality;
    c.stillResIdx   = frameSizeToStillIdx(stillFrameSize);
    c.previewResIdx = (currentFrameSize == FRAMESIZE_VGA) ? 1 : 0;
    c.gridMode      = gridMode;
    c.scenePreset   = (uint8_t)sceneVal;
    c.flipV         = vflip;
    c.mirrorH       = hmirror;
}

void CameraManager::applyAllSettings() {
    if (!initialized) return;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    s->set_brightness(s, brightnessVal);
    s->set_contrast(s, contrastVal);
    s->set_saturation(s, saturationVal);
    s->set_sharpness(s, sharpnessVal);
    s->set_denoise(s, denoiseVal);
    s->set_ae_level(s, exposureVal);
    s->set_gainceiling(s, (gainceiling_t)gainCeilingVal);
    s->set_special_effect(s, (int)currentFilter);
    s->set_wb_mode(s, wbVal);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_vflip(s, vflip ? 1 : 0);
    s->set_hmirror(s, hmirror ? 1 : 0);

    // Professional OV3660 ISP defaults (DSLR-grade clarity)
    s->set_aec2(s, 1);        // multi-zone auto-exposure DSP
    s->set_bpc(s, 1);         // black (dead) pixel correction
    s->set_wpc(s, 1);         // white (hot) pixel correction
    s->set_raw_gma(s, 1);     // gamma curve for rich natural contrast
    s->set_lenc(s, 1);        // lens shading / vignette compensation
    s->set_dcw(s, 1);         // downsize color weighting (clean scaling)
    s->set_framesize(s, currentFrameSize);
}

camera_fb_t* CameraManager::capture() {
    if (!initialized) return nullptr;
    return esp_camera_fb_get();
}

void CameraManager::release(camera_fb_t* fb) {
    if (fb) esp_camera_fb_return(fb);
}

bool CameraManager::saveJpegToSD(camera_fb_t* fb) {
    if (!fb || !SDManager::isMounted()) return false;

    char path[64];
    snprintf(path, sizeof(path), "%s/IMG_%04u.JPG", PATH_DCIM, (unsigned)imageCounter++);

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[CAM] Failed to open %s\n", path);
        return false;
    }
    size_t written = file.write(fb->buf, fb->len);
    file.close();

    Serial.printf("[CAM] Saved Photo: %s (%u bytes)\n", path, (unsigned)written);
    return written == fb->len;
}

framesize_t CameraManager::getFrameSize() { return currentFrameSize; }

void CameraManager::setFrameSize(framesize_t size) {
    currentFrameSize = size;
    if (initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) s->set_framesize(s, size);
    }
}

framesize_t CameraManager::getStillResolution() { return stillFrameSize; }
void CameraManager::setStillResolution(framesize_t size) { stillFrameSize = size; }

void CameraManager::cycleStillResolution() {
    int idx = (frameSizeToStillIdx(stillFrameSize) + 1) % 4;
    stillFrameSize = stillIdxToFrameSize(idx);
}

int CameraManager::getStillResolutionIndex() { return frameSizeToStillIdx(stillFrameSize); }
void CameraManager::setStillResolutionIndex(int idx) {
    if (idx < 0) idx = 0; if (idx > 3) idx = 3;
    stillFrameSize = stillIdxToFrameSize(idx);
}

const char* CameraManager::getStillResolutionName() {
    switch (stillFrameSize) {
        case FRAMESIZE_VGA:  return "VGA 640x480";
        case FRAMESIZE_HD:   return "HD 1280x720";
        case FRAMESIZE_UXGA: return "UXGA 2MP";
        case FRAMESIZE_QXGA: return "QXGA 3MP";
        default:             return "3MP ULTRA";
    }
}

void CameraManager::setStillQuality(int q) {
    if (q < 4) q = 4;
    if (q > 20) q = 20;
    stillQuality = (uint8_t)q;
}
int CameraManager::getStillQuality() { return stillQuality; }
void CameraManager::adjustStillQuality(int delta) { setStillQuality((int)stillQuality + delta); }

bool CameraManager::captureHighResStill(char* savedPathOut, size_t maxPathLen, const char* customBaseName) {
    if (!initialized || !SDManager::isMounted()) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    // 1. Switch sensor to full still resolution & best quality
    s->set_framesize(s, stillFrameSize);
    s->set_quality(s, stillQuality);

    // Flush several frames so PLL, auto-exposure, AGC and AWB fully settle
    for (int i = 0; i < 3; i++) {
        camera_fb_t* dummy = esp_camera_fb_get();
        if (dummy) esp_camera_fb_return(dummy);
        delay(35);
    }

    // 2. Capture the pristine high-resolution photo
    camera_fb_t* photoFb = esp_camera_fb_get();
    bool saveOk = false;
    char path[80];
    if (photoFb) {
        if (customBaseName && strlen(customBaseName) > 0) {
            snprintf(path, sizeof(path), "%s/%s.JPG", PATH_DCIM, customBaseName);
        } else {
            uint32_t idx = SettingsManager::getNextImageIndex();
            const char* pfx = SettingsManager::getImagePrefix();
            if (idx == 1) {
                snprintf(path, sizeof(path), "%s/%s.JPG", PATH_DCIM, pfx);
            } else {
                snprintf(path, sizeof(path), "%s/%s%u.JPG", PATH_DCIM, pfx, (unsigned)idx);
            }
            SettingsManager::incrementImageIndex();
            SettingsManager::save();
        }
        imageCounter++;

        File f = SD_MMC.open(path, FILE_WRITE);
        if (f) {
            size_t written = f.write(photoFb->buf, photoFb->len);
            f.close();
            saveOk = (written == photoFb->len);
            Serial.printf("[CAM] Still Photo Saved: %s (%u bytes)\n", path, (unsigned)written);
            if (savedPathOut && maxPathLen > 0) snprintf(savedPathOut, maxPathLen, "%s", path);
        }
        esp_camera_fb_return(photoFb);
    }

    // 3. Restore fast preview pipeline
    s->set_framesize(s, currentFrameSize);
    s->set_quality(s, 10);
    camera_fb_t* dummy = esp_camera_fb_get();
    if (dummy) esp_camera_fb_return(dummy);

    return saveOk;
}

void CameraManager::cycleResolution() {
    if (currentFrameSize == FRAMESIZE_QVGA) setFrameSize(FRAMESIZE_VGA);
    else setFrameSize(FRAMESIZE_QVGA);
}

const char* CameraManager::getResolutionName() {
    switch (currentFrameSize) {
        case FRAMESIZE_QVGA: return "QVGA 320x240";
        case FRAMESIZE_VGA:  return "VGA 640x480";
        case FRAMESIZE_SVGA: return "SVGA 800x600";
        case FRAMESIZE_XGA:  return "XGA 1024x768";
        default:             return "CUSTOM";
    }
}

void CameraManager::setFilter(CameraFilterMode mode) {
    currentFilter = mode;
    if (initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) s->set_special_effect(s, (int)mode);
    }
}

void CameraManager::cycleFilter() {
    currentFilter = (CameraFilterMode)(((int)currentFilter + 1) % FILTER_COUNT);
    setFilter(currentFilter);
}

const char* CameraManager::getFilterName() { return filterNames[(int)currentFilter]; }
CameraFilterMode CameraManager::getFilter() { return currentFilter; }

void CameraManager::setBrightness(int b) {
    if (b < -2) b = -2; if (b > 2) b = 2;
    brightnessVal = b;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_brightness(s, b); }
}
int CameraManager::getBrightness() { return brightnessVal; }
void CameraManager::adjustBrightness(int delta) { setBrightness(brightnessVal + delta); }

void CameraManager::setContrast(int c) {
    if (c < -2) c = -2; if (c > 2) c = 2;
    contrastVal = c;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_contrast(s, c); }
}
int CameraManager::getContrast() { return contrastVal; }
void CameraManager::adjustContrast(int delta) { setContrast(contrastVal + delta); }

void CameraManager::setSaturation(int val) {
    if (val < -2) val = -2; if (val > 2) val = 2;
    saturationVal = val;
    if (initialized) {
        sensor_t* sensor = esp_camera_sensor_get();
        if (sensor) sensor->set_saturation(sensor, val);
    }
}
int CameraManager::getSaturation() { return saturationVal; }
void CameraManager::adjustSaturation(int delta) { setSaturation(saturationVal + delta); }

void CameraManager::setSharpness(int val) {
    if (val < -3) val = -3; if (val > 3) val = 3;
    sharpnessVal = val;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_sharpness(s, val); }
}
int CameraManager::getSharpness() { return sharpnessVal; }
void CameraManager::adjustSharpness(int delta) { setSharpness(sharpnessVal + delta); }

void CameraManager::setDenoise(int d) {
    if (d < 0) d = 0; if (d > 8) d = 8;
    denoiseVal = d;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_denoise(s, d); }
}
int CameraManager::getDenoise() { return denoiseVal; }
void CameraManager::adjustDenoise(int delta) { setDenoise(denoiseVal + delta); }

void CameraManager::setExposure(int ae_level) {
    if (ae_level < -2) ae_level = -2; if (ae_level > 2) ae_level = 2;
    exposureVal = ae_level;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_ae_level(s, ae_level); }
}
int CameraManager::getExposure() { return exposureVal; }
void CameraManager::adjustExposure(int delta) { setExposure(exposureVal + delta); }

void CameraManager::setGainCeiling(int idx) {
    if (idx < 0) idx = 0; if (idx > 6) idx = 6;
    gainCeilingVal = idx;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_gainceiling(s, (gainceiling_t)idx); }
}
int CameraManager::getGainCeiling() { return gainCeilingVal; }
void CameraManager::adjustGainCeiling(int delta) { setGainCeiling(gainCeilingVal + delta); }

void CameraManager::setWbMode(int mode) {
    if (mode < 0) mode = 0; if (mode > 4) mode = 4;
    wbVal = mode;
    if (initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_wb_mode(s, mode);
            s->set_whitebal(s, 1);
            s->set_awb_gain(s, 1);
        }
    }
}
int CameraManager::getWbMode() { return wbVal; }
void CameraManager::cycleWbMode() { setWbMode((wbVal + 1) % 5); }
const char* CameraManager::getWbModeName() { return wbNames[wbVal]; }

void CameraManager::applySceneToSensor(CameraScene scene) {
    // Scene presets drive the exposure/gain/denoise trio; user sliders for
    // brightness/contrast/saturation/sharpness are preserved.
    switch (scene) {
        case SCENE_INDOOR:  gainCeilingVal = 3; denoiseVal = 6; exposureVal = 1;  wbVal = 0; break; // 16X
        case SCENE_OUTDOOR: gainCeilingVal = 0; denoiseVal = 2; exposureVal = 0;  wbVal = 1; break; // 2X, sunny
        case SCENE_NIGHT:   gainCeilingVal = 6; denoiseVal = 8; exposureVal = 2;  wbVal = 0; break; // 128X
        case SCENE_MACRO:   gainCeilingVal = 1; denoiseVal = 4; exposureVal = 0;  wbVal = 0; break; // 4X
        case SCENE_AUTO:
        default:            gainCeilingVal = 2; denoiseVal = 4; exposureVal = 0;  wbVal = 0; break; // 8X
    }
    if (initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_gainceiling(s, (gainceiling_t)gainCeilingVal);
            s->set_denoise(s, denoiseVal);
            s->set_ae_level(s, exposureVal);
            s->set_wb_mode(s, wbVal);
            s->set_whitebal(s, 1);
            s->set_awb_gain(s, 1);
        }
    }
}

void CameraManager::setScene(CameraScene scene) {
    sceneVal = scene;
    applySceneToSensor(scene);
}
CameraScene CameraManager::getScene() { return sceneVal; }
void CameraManager::cycleScene() { setScene((CameraScene)(((int)sceneVal + 1) % SCENE_COUNT)); }
const char* CameraManager::getSceneName() { return sceneNames[(int)sceneVal]; }

void CameraManager::toggleHMirror() {
    hmirror = !hmirror;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_hmirror(s, hmirror ? 1 : 0); }
}
void CameraManager::toggleVFlip() {
    vflip = !vflip;
    if (initialized) { sensor_t* s = esp_camera_sensor_get(); if (s) s->set_vflip(s, vflip ? 1 : 0); }
}
bool CameraManager::getHMirror() { return hmirror; }
bool CameraManager::getVFlip() { return vflip; }

float CameraManager::getZoom() { return zoomLevel; }
void CameraManager::setZoom(float z) {
    if (z < 1.0f) z = 1.0f; if (z > 3.0f) z = 3.0f;
    zoomLevel = z;
}
void CameraManager::adjustZoom(float dz) { setZoom(zoomLevel + dz); }
void CameraManager::getPan(int& panX, int& panY) { panX = panOffsetX; panY = panOffsetY; }
void CameraManager::setPan(int panX, int panY) { panOffsetX = panX; panOffsetY = panY; }

bool CameraManager::captureBurst(uint8_t count) {
    if (!initialized || !SDManager::isMounted()) return false;
    bool allSuccess = true;
    for (uint8_t i = 0; i < count; i++) {
        camera_fb_t* fb = capture();
        if (fb) {
            bool saved = saveJpegToSD(fb);
            release(fb);
            if (!saved) allSuccess = false;
        } else {
            allSuccess = false;
        }
        delay(60);
    }
    return allSuccess;
}

void CameraManager::startTimelapse(uint32_t intervalSec) {
    timelapseIntervalSec = (intervalSec < 1) ? 1 : intervalSec;
    timelapseActive = true;
    timelapseCount = 0;
    lastTimelapseCaptureMs = millis() - (timelapseIntervalSec * 1000);
    Serial.printf("[CAM] Timelapse started (Interval: %u sec)\n", (unsigned)timelapseIntervalSec);
}
void CameraManager::stopTimelapse() {
    timelapseActive = false;
    Serial.printf("[CAM] Timelapse stopped (Frames: %u)\n", (unsigned)timelapseCount);
}
bool CameraManager::isTimelapseRunning() { return timelapseActive; }

bool CameraManager::tickTimelapse() {
    if (!timelapseActive || !initialized || !SDManager::isMounted()) return false;
    uint32_t now = millis();
    if (now - lastTimelapseCaptureMs >= (timelapseIntervalSec * 1000)) {
        lastTimelapseCaptureMs = now;
        camera_fb_t* fb = capture();
        if (fb) {
            bool ok = saveJpegToSD(fb);
            release(fb);
            if (ok) timelapseCount++;
            return ok;
        }
    }
    return false;
}

// 220-byte Motion-JPEG AVI container header template. Width/height and timing
// are patched at start/stop so clips carry correct dimensions and playback fps.
static uint8_t aviHeaderTemplate[220] = {
    'R','I','F','F', 0,0,0,0, 'A','V','I',' ',
    'L','I','S','T', 0x44,0,0,0, 'h','d','r','l',
    'a','v','i','h', 0x38,0,0,0,
    0x40,0x42,0x0F,0x00,   // 32: microsec per frame (patched)
    0,0,0,0,               // 36: max bytes/sec
    0,0,0,0,               // 40: reserved
    0x10,0,0,0,            // 44: flags AVIF_HASINDEX
    0,0,0,0,               // 48: total frames (patched)
    0,0,0,0,               // 52: initial frames
    0x01,0,0,0,            // 56: streams
    0,0,0,0,               // 60: suggested buffer
    0x40,0x01,0,0,         // 64: width (patched)
    0xF0,0x00,0,0,         // 68: height (patched)
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    'L','I','S','T', 0x74,0,0,0, 's','t','r','l',
    's','t','r','h', 0x38,0,0,0,
    'v','i','d','s', 'M','J','P','G',
    0,0,0,0,               // 116: flags
    0,0,0,0,               // 120
    0,0,0,0,               // 124
    0x01,0,0,0,            // 128: scale (timescale)
    0x16,0,0,0,            // 132: rate (fps, patched)
    0,0,0,0,               // 136: start
    0,0,0,0,               // 140: length (patched)
    0,0,0,0,               // 144: suggested buffer
    0xFF,0xFF,0xFF,0xFF,   // 148: quality
    0,0,0,0,               // 152: sample size
    0,0,0,0,               // 156: frame rect
    0x40,0x01,0xF0,0x00,   // 160: rcFrame right/bottom (patched)
    's','t','r','f', 0x28,0,0,0,
    0x40,0x01,0,0,         // 172: biWidth (patched)
    0xF0,0x00,0,0,         // 176: biHeight (patched)
    0x01,0x00,             // 180: biPlanes
    0x18,0x00,             // 182: biBitCount
    'M','J','P','G',       // 184: biCompression
    0,0,0,0,               // 188: biSizeImage
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    'L','I','S','T', 0,0,0,0, 'm','o','v','i'
};

static void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void put32(uint8_t* p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }

bool CameraManager::isRecordingVideo() { return recordingVideo; }

bool CameraManager::startVideoRecord() {
    if (recordingVideo || !SDManager::isMounted()) return false;

    // Resolve actual preview dimensions for the header
    switch (currentFrameSize) {
        case FRAMESIZE_VGA: videoWidth = 640; videoHeight = 480; break;
        case FRAMESIZE_QVGA:
        default:            videoWidth = 320; videoHeight = 240; break;
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/VID_%04u.AVI", PATH_DCIM, (unsigned)imageCounter++);
    videoFile = SD_MMC.open(path, FILE_WRITE);
    if (!videoFile) {
        Serial.printf("[CAM] Failed to open video file %s\n", path);
        return false;
    }

    // Patch dimensions into the template before writing
    put32(&aviHeaderTemplate[64], videoWidth);
    put32(&aviHeaderTemplate[68], videoHeight);
    put16(&aviHeaderTemplate[160], videoWidth);   // rcFrame right
    put16(&aviHeaderTemplate[162], videoHeight);  // rcFrame bottom
    put32(&aviHeaderTemplate[172], videoWidth);   // biWidth
    put32(&aviHeaderTemplate[176], videoHeight);  // biHeight
    put32(&aviHeaderTemplate[32], 1000000UL / 20);  // provisional 20 fps

    videoFile.write(aviHeaderTemplate, 220);
    videoFrameCount = 0;
    videoBytesWritten = 0;
    videoStartTimeMs = millis();
    recordingVideo = true;
    Serial.printf("[CAM] AVI Recording Started: %s (%ux%u)\n", path, videoWidth, videoHeight);
    return true;
}

bool CameraManager::stopVideoRecord() {
    if (!recordingVideo) return false;
    recordingVideo = false;
    if (!videoFile) return true;

    uint32_t durMs = millis() - videoStartTimeMs;
    if (durMs == 0) durMs = 1;
    float fps = (float)videoFrameCount * 1000.0f / (float)durMs;
    if (fps < 1.0f) fps = 1.0f;
    if (fps > 60.0f) fps = 60.0f;
    uint32_t usPerFrame = (uint32_t)(1000000.0f / fps);
    uint32_t rateFps = (uint32_t)(fps + 0.5f);

    uint32_t fileSize = videoFile.size();
    uint32_t riffSize = (fileSize > 8) ? (fileSize - 8) : 0;
    // movi LIST size counts bytes after the 'movi' fourcc (file offset 216)
    uint32_t moviListSize = (fileSize > 216) ? (fileSize - 216) : 0;

    uint8_t buf4[4];

    videoFile.seek(4);   put32(buf4, riffSize);           videoFile.write(buf4, 4);
    videoFile.seek(32);  put32(buf4, usPerFrame);         videoFile.write(buf4, 4);
    videoFile.seek(48);  put32(buf4, videoFrameCount);    videoFile.write(buf4, 4);
    videoFile.seek(132); put32(buf4, rateFps);            videoFile.write(buf4, 4);
    videoFile.seek(140); put32(buf4, videoFrameCount);    videoFile.write(buf4, 4);
    videoFile.seek(212); put32(buf4, moviListSize);       videoFile.write(buf4, 4);

    videoFile.flush();
    videoFile.close();

    Serial.printf("[CAM] AVI Saved: %u frames, %.1fs, %.1f fps\n",
                  (unsigned)videoFrameCount, durMs / 1000.0f, fps);
    return true;
}

bool CameraManager::appendVideoFrame(camera_fb_t* fb) {
    if (!recordingVideo || !videoFile || !fb) return false;

    // Guard against FAT32 2GB limit and low free space
    if (videoBytesWritten > 1900UL * 1024UL * 1024UL) {
        Serial.println("[CAM] Video near 2GB limit, stopping");
        stopVideoRecord();
        return false;
    }

    const uint8_t chunkHeader[4] = { '0', '0', 'd', 'c' };
    uint32_t len = fb->len;
    uint32_t paddedLen = (len + 1) & ~1; // 2-byte boundary alignment

    uint8_t sz[4];
    videoFile.write(chunkHeader, 4);
    put32(sz, len);
    videoFile.write(sz, 4);
    videoFile.write(fb->buf, len);
    if (paddedLen > len) { uint8_t zero = 0; videoFile.write(&zero, 1); }

    videoBytesWritten += 8 + paddedLen;
    videoFrameCount++;
    return true;
}

uint32_t CameraManager::getVideoElapsedSec() {
    if (!recordingVideo) return 0;
    return (millis() - videoStartTimeMs) / 1000;
}
uint32_t CameraManager::getVideoFramesRecorded() { return videoFrameCount; }
uint32_t CameraManager::getVideoFileSizeKB() { return videoBytesWritten / 1024; }

void CameraManager::setIsoGain(int gainCeiling) { setGainCeiling(gainCeiling); }
void CameraManager::setSpecialEffect(CameraFilterMode mode) { setFilter(mode); }

void CameraManager::optimizeSensorForLighting(bool indoor) {
    setScene(indoor ? SCENE_INDOOR : SCENE_OUTDOOR);
}

void CameraManager::cycleGridMode() { gridMode = (gridMode + 1) % 4; }
uint8_t CameraManager::getGridMode() { return gridMode; }
const char* CameraManager::getGridModeName() {
    switch (gridMode) {
        case 0: return "NONE";
        case 1: return "RETICLE";
        case 2: return "RULE 3RDS";
        case 3: return "HORIZON";
        default: return "NONE";
    }
}

uint32_t CameraManager::getImageCount() { return imageCounter; }

bool CameraManager::getRecentImagePath(char* outPath, size_t maxLen, int offsetFromLatest) {
    if (!SDManager::isMounted() || imageCounter == 0) return false;
    int target = (int)imageCounter - 1 - offsetFromLatest;
    if (target < 0) target = 0;
    snprintf(outPath, maxLen, "%s/IMG_%04d.JPG", PATH_DCIM, target);
    return SD_MMC.exists(outPath);
}

bool CameraManager::deleteImage(int offsetFromLatest) {
    char path[64];
    if (getRecentImagePath(path, sizeof(path), offsetFromLatest)) {
        bool ok = SD_MMC.remove(path);
        if (ok && imageCounter > 0) imageCounter--;
        return ok;
    }
    return false;
}
