#pragma once

#include <Arduino.h>

// Forward declarations
class SDLogger;
class WifiConnect;
class BootBootsBluetoothService;
class BluetoothOTA;
class OTAUpdate;
class PCF8574Manager;
class AWSAuth;
class Camera;
class VideoRecorder;
class ImageStorage;
class LedController;
class CaptureController;
class InputManager;
class MotionDetector;
class DeterrentController;
struct SystemState;

/**
 * SystemManager - Orchestrates system initialization and runtime management
 *
 * Consolidates hardware initialization, component setup, and main loop
 * handling into a single coordinated manager. Uses dependency injection
 * to maintain loose coupling with system components.
 */
class SystemManager {
public:
    /**
     * Configuration passed during initialization
     */
    struct Config {
        int i2cSDA;
        int i2cSCL;
        uint8_t pcf8574Address;
        int bootButtonPin;
        unsigned long debounceMs;
        const char* imagesDir;
        int maxImagesToKeep;
        const char* deviceName;
        const char* awsRegion;
        const char* awsRoleAlias;
        const char* apiHost;
        const char* apiPath;
    };

    SystemManager();
    ~SystemManager();

    /**
     * Initialize all hardware (I2C, input manager, etc.)
     * @param config System configuration
     * @param state Reference to system state struct
     * @param inputManager Reference to input manager instance
     * @return true if hardware initialization succeeded
     */
    bool initHardware(const Config& config, SystemState& state, InputManager& inputManager);

    /**
     * Initialize all system components (WiFi, BLE, camera, etc.)
     * @param config System configuration
     * @param state Reference to system state struct
     * @param ledController Reference to LED controller
     * @param inputManager Reference to input manager
     * @return true if all critical components initialized
     */
    bool initComponents(const Config& config, SystemState& state,
                        LedController& ledController, InputManager& inputManager);

    /**
     * Update system state - call in main loop()
     * Handles Bluetooth, OTA, WiFi status monitoring
     * @param state Reference to system state struct
     */
    void update(SystemState& state);

    /**
     * Log and handle a system error
     * @param component Name of the component that failed
     * @param error Error message
     * @param state Reference to system state struct
     */
    void handleError(const char* component, const char* error, SystemState& state);

    /**
     * Blink the PCF8574 LED if available
     * Call periodically from loop for heartbeat indication
     * @param state Reference to system state struct
     */
    void blinkPcfLed(SystemState& state);

    // Accessors for components (needed by main.cpp for specific operations)
    BootBootsBluetoothService* getBluetoothService() { return _bluetoothService; }
    BluetoothOTA* getBluetoothOTA() { return _bluetoothOTA; }
    OTAUpdate* getOtaUpdate() { return _otaUpdate; }
    PCF8574Manager* getPcfManager() { return _pcfManager; }
    WifiConnect* getWifiConnect() { return _wifiConnect; }
    AWSAuth* getAwsAuth() { return _awsAuth; }
    Camera* getCamera() { return _camera; }
    VideoRecorder* getVideoRecorder() { return _videoRecorder; }
    ImageStorage* getImageStorage() { return _imageStorage; }
    CaptureController* getCaptureController() { return _captureController; }
    MotionDetector* getMotionDetector() { return _motionDetector; }
    DeterrentController* getDeterrentController() { return _deterrentController; }

private:
    // Owned components (created and destroyed by SystemManager)
    WifiConnect* _wifiConnect;
    BootBootsBluetoothService* _bluetoothService;
    BluetoothOTA* _bluetoothOTA;
    OTAUpdate* _otaUpdate;
    PCF8574Manager* _pcfManager;
    AWSAuth* _awsAuth;
    Camera* _camera;
    VideoRecorder* _videoRecorder;
    ImageStorage* _imageStorage;
    CaptureController* _captureController;
    MotionDetector* _motionDetector;
    DeterrentController* _deterrentController;

    // PCF8574 LED blinking state
    unsigned long _lastPcfBlink;
    bool _pcfLedState;

    // WiFi status monitoring
    void updateWifiStatus(SystemState& state);
};
