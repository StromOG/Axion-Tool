#pragma once
#include "esp_camera.h"
#include <stddef.h>

enum CameraFilterMode {
    FILTER_NORMAL = 0,
    FILTER_NEGATIVE,
    FILTER_GRAYSCALE,
    FILTER_RED_TINT,
    FILTER_GREEN_TINT,
    FILTER_BLUE_TINT,
    FILTER_SEPIA,
    FILTER_SOLARIZE,
    FILTER_COUNT
};

enum CameraScene {
    SCENE_AUTO = 0,
    SCENE_INDOOR,
    SCENE_OUTDOOR,
    SCENE_NIGHT,
    SCENE_MACRO,
    SCENE_COUNT
};

class CameraManager {
public:
    static bool init();
    static void deinit();
    static bool isInitialized();
    static camera_fb_t* capture();
    static void release(camera_fb_t* fb);
    static bool saveJpegToSD(camera_fb_t* fb);

    // Pull persisted config from SettingsManager and push live state back to it
    static void loadFromSettings();
    static void storeToSettings();
    static void applyAllSettings();   // re-apply every ISP param to the sensor

    // Resolution Control
    static framesize_t getFrameSize();
    static void setFrameSize(framesize_t size);
    static void cycleResolution();
    static const char* getResolutionName();

    // Sensor Controls & Cyber Effects
    static void setFilter(CameraFilterMode mode);
    static void cycleFilter();
    static const char* getFilterName();
    static CameraFilterMode getFilter();

    static void setBrightness(int b);
    static int getBrightness();
    static void adjustBrightness(int delta);

    static void setContrast(int c);
    static int getContrast();
    static void adjustContrast(int delta);

    static void setSaturation(int s);
    static int getSaturation();
    static void adjustSaturation(int delta);

    static void setSharpness(int s);
    static int getSharpness();
    static void adjustSharpness(int delta);

    static void setDenoise(int d);
    static int getDenoise();
    static void adjustDenoise(int delta);

    static void setExposure(int ae_level);
    static int getExposure();
    static void adjustExposure(int delta);

    static void setGainCeiling(int idx);   // 0..6
    static int getGainCeiling();
    static void adjustGainCeiling(int delta);

    static void setWbMode(int mode);
    static int getWbMode();
    static void cycleWbMode();
    static const char* getWbModeName();

    // Scene presets (coherent gain/AE/denoise/AWB sets)
    static void setScene(CameraScene scene);
    static CameraScene getScene();
    static void cycleScene();
    static const char* getSceneName();

    // Hardware Flip & Mirror
    static void toggleHMirror();
    static void toggleVFlip();
    static bool getHMirror();
    static bool getVFlip();

    // Digital Zoom & Pan State
    static float getZoom();
    static void setZoom(float z);
    static void adjustZoom(float dz);
    static void getPan(int& panX, int& panY);
    static void setPan(int panX, int panY);

    // Advanced Capture Modes: High-Res Dual Pipeline (QVGA for Viewfinder, HD/QXGA for Still Capture)
    static framesize_t getStillResolution();
    static void setStillResolution(framesize_t size);
    static void cycleStillResolution();
    static const char* getStillResolutionName();
    static int getStillResolutionIndex();     // 0..3
    static void setStillResolutionIndex(int idx);

    static void setStillQuality(int q);        // 4..20 (lower = better)
    static int getStillQuality();
    static void adjustStillQuality(int delta);

    static bool captureHighResStill(char* savedPathOut = nullptr, size_t maxPathLen = 0, const char* customBaseName = nullptr);
    static bool captureBurst(uint8_t count = 3);
    static void startTimelapse(uint32_t intervalSec);
    static void stopTimelapse();
    static bool isTimelapseRunning();
    static bool tickTimelapse(); // Called in loop to trigger interval captures

    // Video Recording: AVI Container with Motion-JPEG Video Stream
    static bool isRecordingVideo();
    static bool startVideoRecord();
    static bool stopVideoRecord();
    static bool appendVideoFrame(camera_fb_t* fb);
    static uint32_t getVideoElapsedSec();
    static uint32_t getVideoFramesRecorded();
    static uint32_t getVideoFileSizeKB();

    // Advanced Sensor & ISP Tuning
    static void setIsoGain(int gainCeiling);
    static void setSpecialEffect(CameraFilterMode mode);
    static void optimizeSensorForLighting(bool indoor);

    // Grid and Telemetry Overlay Toggles
    static void cycleGridMode();
    static uint8_t getGridMode(); // 0 = None, 1 = Reticle, 2 = Rule of Thirds, 3 = Horizon
    static const char* getGridModeName();

    // Gallery Support
    static uint32_t getImageCount();
    static bool getRecentImagePath(char* outPath, size_t maxLen, int offsetFromLatest = 0);
    static bool deleteImage(int offsetFromLatest);

private:
    static void applySceneToSensor(CameraScene scene);

    static bool initialized;
    static framesize_t currentFrameSize;
    static framesize_t stillFrameSize;
    static uint8_t stillQuality;
    static uint32_t imageCounter;
    static CameraFilterMode currentFilter;
    static int brightnessVal;
    static int contrastVal;
    static int saturationVal;
    static int sharpnessVal;
    static int denoiseVal;
    static int exposureVal;
    static int gainCeilingVal;
    static int wbVal; // 0=Auto, 1=Sunny, 2=Cloudy, 3=Office, 4=Home
    static CameraScene sceneVal;
    static bool hmirror;
    static bool vflip;
    static float zoomLevel;
    static int panOffsetX;
    static int panOffsetY;
    static uint8_t gridMode;

    // Timelapse State
    static bool timelapseActive;
    static uint32_t timelapseIntervalSec;
    static uint32_t lastTimelapseCaptureMs;
    static uint32_t timelapseCount;

    // Video Recording State
    static bool recordingVideo;
    static File videoFile;
    static uint32_t videoFrameCount;
    static uint32_t videoStartTimeMs;
    static uint16_t videoWidth;
    static uint16_t videoHeight;
    static uint32_t videoBytesWritten;
};
