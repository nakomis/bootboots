#ifndef CATCAM_SYSTEMSTATE_H
#define CATCAM_SYSTEMSTATE_H

// SystemState struct definition - shared between main.cpp and BluetoothService
struct SystemState {
    bool initialized = false;
    bool cameraReady = false;
    bool wifiConnected = false;
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
};

#endif
