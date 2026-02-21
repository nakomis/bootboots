#ifndef CATCAM_SYSTEMSTATE_H
#define CATCAM_SYSTEMSTATE_H

// Camera sensor settings - synced via BLE and persisted to NVS
struct CameraSettings {
    int frameSize = 13;        // framesize_t enum: 5=QVGA, 8=VGA, 9=SVGA, 10=XGA, 11=HD, 12=SXGA, 13=UXGA
    int jpegQuality = 10;      // 0-63 (lower = better quality, more data)
    int fbCount = 2;           // 1-3 frame buffers (requires PSRAM for >1)
    int brightness = 0;        // -2 to 2
    int contrast = 0;          // -2 to 2
    int saturation = 0;        // -2 to 2
    int specialEffect = 0;     // 0=None, 1=Negative, 2=Grayscale, 3=Red, 4=Green, 5=Blue, 6=Sepia
    bool whiteBalance = true;
    bool awbGain = true;
    int wbMode = 0;            // 0=Auto, 1=Sunny, 2=Cloudy, 3=Office, 4=Home
    bool exposureCtrl = true;
    bool aec2 = false;
    int aeLevel = -2;          // -2 to 2
    int aecValue = 300;        // 0 to 1200
    bool gainCtrl = true;
    int agcGain = 15;          // 0 to 30
    int gainCeiling = 0;       // 0 to 6
    bool bpc = false;
    bool wpc = true;
    bool rawGma = true;
    bool lenc = true;
    bool hmirror = false;
    bool vflip = false;
    bool dcw = true;
    bool colorbar = false;
    int ledDelayMillis = 100;
};

// SystemState struct definition - shared between main.cpp and BluetoothService
struct SystemState {
    bool initialized = false;
    bool cameraReady = false;
    bool wifiConnected = false;
    bool mqttConnected = false;
    bool sdCardReady = false;
    bool i2cReady = false;
    bool pcf8574Ready = false;
    bool atomizerEnabled = true;
    unsigned long lastDetection = 0;
    unsigned long lastStatusReport = 0;
    unsigned long systemStartTime = 0;
    int totalDetections = 0;
    int bootsDetections = 0;
    int falsePositivesAvoided = 0;
    int atomizerActivations = 0;

    // Motion detection tracking
    int motionTriggerCount = 0;         // Total PIR motion events detected
    int deterrentActivationCount = 0;   // Times deterrent was activated (Boots detected)

    // Training mode - captures photos without inference/deterrent
    bool trainingMode = false;

    // Deterrent settings (persisted to NVS, configurable via MQTT)
    float triggerThresh = 0.80f;  // Boots confidence required to fire deterrent (0-1)
    bool dryRun = false;          // When true, skip atomizer but run all other steps
    bool claudeInfer = false;     // When true, send ?claude=1 to infer Lambda for parallel Claude vision

    // Camera sensor settings
    CameraSettings cameraSettings;
};

#endif
