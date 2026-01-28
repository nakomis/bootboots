#include "SystemManager.h"

#include <Wire.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <SDLogger.h>

#include "SystemState.h"
#include "WifiConnect.h"
#include "BluetoothService.h"
#include "BluetoothOTA.h"
#include "OTAUpdate.h"
#include "PCF8574Manager.h"
#include "AWSAuth.h"
#include "Camera.h"
#include "VideoRecorder.h"
#include "ImageStorage.h"
#include "LedController.h"
#include "CaptureController.h"
#include "InputManager.h"
#include "MotionDetector.h"
#include "DeterrentController.h"
#include "secrets.h"

SystemManager::SystemManager()
    : _wifiConnect(nullptr)
    , _bluetoothService(nullptr)
    , _bluetoothOTA(nullptr)
    , _otaUpdate(nullptr)
    , _pcfManager(nullptr)
    , _awsAuth(nullptr)
    , _camera(nullptr)
    , _videoRecorder(nullptr)
    , _imageStorage(nullptr)
    , _captureController(nullptr)
    , _motionDetector(nullptr)
    , _deterrentController(nullptr)
    , _lastPcfBlink(0)
    , _pcfLedState(false)
{
}

SystemManager::~SystemManager() {
    delete _deterrentController;
    delete _motionDetector;
    delete _captureController;
    delete _imageStorage;
    delete _videoRecorder;
    delete _camera;
    delete _awsAuth;
    delete _pcfManager;
    delete _bluetoothOTA;
    delete _bluetoothService;
    delete _otaUpdate;
    delete _wifiConnect;
}

bool SystemManager::initHardware(const Config& config, SystemState& state, InputManager& inputManager) {
    SDLogger::getInstance().infof("Initializing hardware...");

    // Initialize input manager for BOOT button
    inputManager.init(config.bootButtonPin, config.debounceMs);
    SDLogger::getInstance().infof("Input manager initialized (BOOT button on GPIO%d)", config.bootButtonPin);

    // Enable internal pull-ups on I2C pins before initializing I2C
    pinMode(config.i2cSDA, INPUT_PULLUP);
    pinMode(config.i2cSCL, INPUT_PULLUP);

    // Initialize I2C with custom pins
    Wire.begin(config.i2cSDA, config.i2cSCL);
    Wire.setClock(100000); // 100kHz I2C clock for reliability
    state.i2cReady = true;
    SDLogger::getInstance().infof("I2C initialized on GPIO%d (SDA) and GPIO%d (SCL) with internal pull-ups",
                                   config.i2cSDA, config.i2cSCL);

    SDLogger::getInstance().infof("Hardware initialization complete");
    return true;
}

bool SystemManager::initComponents(const Config& config, SystemState& state,
                                    LedController& ledController, InputManager& inputManager) {
    SDLogger::getInstance().infof("Initializing system components...");

    // Check if SDLogger is initialized
    if (SDLogger::getInstance().isInitialized()) {
        state.sdCardReady = true;
        SDLogger::getInstance().infof("SD Logger initialized successfully");

        // Initialize image storage for captured photos
        _imageStorage = new ImageStorage();
        if (_imageStorage->init(config.imagesDir, config.maxImagesToKeep)) {
            SDLogger::getInstance().infof("Image storage initialized");
        } else {
            SDLogger::getInstance().warnf("Image storage initialization failed");
        }
    } else {
        state.sdCardReady = false;
    }

    // Initialize PCF8574 Manager
    _pcfManager = new PCF8574Manager(config.pcf8574Address);
    if (_pcfManager->init(config.i2cSDA, config.i2cSCL)) {
        state.pcf8574Ready = true;
        SDLogger::getInstance().infof("PCF8574 Manager initialized successfully at address 0x%02X",
                                       config.pcf8574Address);
        if (state.sdCardReady) {
            SDLogger::getInstance().infof("PCF8574 Manager initialized - 8 GPIO pins available");
        }
    } else {
        state.pcf8574Ready = false;
        SDLogger::getInstance().warnf("WARNING: PCF8574 Manager initialization failed");
        SDLogger::getInstance().warnf("Check I2C connections on GPIO%d (SDA) and GPIO%d (SCL)",
                                       config.i2cSDA, config.i2cSCL);
    }

    // Initialize WiFi
    _wifiConnect = new WifiConnect();
    if (_wifiConnect->connect() == 0) {
        state.wifiConnected = true;
        SDLogger::getInstance().infof("WiFi connected successfully");
        SDLogger::getInstance().infof("IP Address: %s", WiFi.localIP().toString().c_str());

        // Initialize AWS Auth
        _awsAuth = new AWSAuth(config.awsRegion);
        if (_awsAuth->initialize(AWS_CERT_CA, AWS_CERT_CRT, AWS_CERT_PRIVATE, AWS_IOT_CREDENTIALS_ENDPOINT)) {
            SDLogger::getInstance().infof("AWS Auth initialized successfully");

            // Create camera and video recorder instances
            _camera = new Camera();
            _videoRecorder = new VideoRecorder();

            // Initialize CaptureController with all dependencies
            _captureController = new CaptureController(_camera, _videoRecorder, &ledController,
                                                        _imageStorage, _awsAuth);
            _captureController->setAWSConfig(config.awsRoleAlias, config.apiHost, config.apiPath);
            _captureController->init();

            // Set callbacks for background task handling during LED animations
            _captureController->setCallbacks(
                [&inputManager]() { return inputManager.isBootButtonPressed(); },
                [this]() {
                    if (_bluetoothService) _bluetoothService->handle();
                    if (_bluetoothOTA) _bluetoothOTA->handle();
                }
            );

            SDLogger::getInstance().infof("=== Press BOOT to record video ===");
        } else {
            SDLogger::getInstance().errorf("Failed to initialize AWS Auth");
        }
    } else {
        SDLogger::getInstance().warnf("WARNING: WiFi connection failed");
        state.wifiConnected = false;
    }

    // Initialize Bluetooth Service
    _bluetoothService = new BootBootsBluetoothService();
    _bluetoothService->init(config.deviceName);
    _bluetoothService->setLedController(&ledController);
    SDLogger::getInstance().infof("Bluetooth Service initialized");

    // Initialize OTA Update Service
    _otaUpdate = new OTAUpdate();
    _otaUpdate->setUpdateCallback([](bool success, const char* error) {
        if (success) {
            SDLogger::getInstance().infof("OTA Update: %s", error);
        } else {
            SDLogger::getInstance().errorf("OTA Update failed: %s", error);
        }
    });
    SDLogger::getInstance().infof("OTA Update service initialized");
    if (state.sdCardReady) {
        SDLogger::getInstance().infof("OTA service initialized - updates available via WiFi");
    }

    // Initialize Bluetooth OTA Service using the shared BLE server
    _bluetoothOTA = new BluetoothOTA();
    if (_bluetoothOTA->initWithExistingServer(_bluetoothService->getServer())) {
        _bluetoothOTA->setOTAUpdate(_otaUpdate);
        SDLogger::getInstance().infof("Bluetooth OTA service initialized with shared BLE server");
        if (state.sdCardReady) {
            SDLogger::getInstance().infof("Bluetooth OTA enabled - remote updates via web interface");
        }
    } else {
        SDLogger::getInstance().errorf("Failed to initialize Bluetooth OTA service");
        delete _bluetoothOTA;
        _bluetoothOTA = nullptr;
    }

    // Start BLE advertising
    BLEDevice::startAdvertising();
    SDLogger::getInstance().infof("BLE advertising started");

    // Initialize Motion Detector (requires PCF8574Manager)
    if (_pcfManager && state.pcf8574Ready) {
        _motionDetector = new MotionDetector(_pcfManager);
        SDLogger::getInstance().infof("Motion Detector initialized on PCF8574 pin P%d",
                                       PCF8574Manager::PIR_SENSOR_PIN);

        // Initialize Deterrent Controller (requires PCF8574Manager, CaptureController, and AWSAuth)
        if (_captureController && _awsAuth) {
            _deterrentController = new DeterrentController(_pcfManager, _captureController, _awsAuth);
            _deterrentController->setUploadConfig("api.bootboots.sandbox.nakomis.com");
            SDLogger::getInstance().infof("Deterrent Controller initialized (threshold: %.0f%%, duration: %lu ms)",
                                           DeterrentController::CONFIDENCE_THRESHOLD * 100.0f,
                                           DeterrentController::DETERRENT_DURATION_MS);
            SDLogger::getInstance().infof("Video upload enabled to api.bootboots.sandbox.nakomis.com");
        } else {
            SDLogger::getInstance().warnf("Deterrent Controller not initialized - CaptureController or AWSAuth unavailable");
        }
    } else {
        SDLogger::getInstance().warnf("Motion Detector not initialized - PCF8574 unavailable");
    }

    return true;
}

void SystemManager::update(SystemState& state) {
    // Handle Bluetooth service
    if (_bluetoothService) {
        _bluetoothService->handle();
    }

    if (_bluetoothOTA) {
        _bluetoothOTA->handle();
    }

    // Update motion detector state
    if (_motionDetector) {
        _motionDetector->update();
    }

    // Update WiFi connection status
    updateWifiStatus(state);
}

void SystemManager::updateWifiStatus(SystemState& state) {
    if (state.wifiConnected && WiFi.status() != WL_CONNECTED) {
        state.wifiConnected = false;
        SDLogger::getInstance().warnf("WARNING: WiFi connection lost");
    } else if (!state.wifiConnected && WiFi.status() == WL_CONNECTED) {
        state.wifiConnected = true;
        SDLogger::getInstance().infof("WiFi connection restored");
    }
}

void SystemManager::handleError(const char* component, const char* error, SystemState& state) {
    SDLogger::getInstance().errorf("SYSTEM ERROR in %s: %s", component, error);

    // Log error to SD card if available
    if (state.sdCardReady) {
        char errorMessage[128];
        snprintf(errorMessage, sizeof(errorMessage), "ERROR in %s: %s", component, error);
        SDLogger::getInstance().errorf("%s", errorMessage);
    }
}

void SystemManager::blinkPcfLed(SystemState& state) {
    if (_pcfManager && state.pcf8574Ready) {
        if (millis() - _lastPcfBlink > 2000) {
            _pcfLedState = !_pcfLedState;
            _pcfManager->setFlashLED(_pcfLedState);
            SDLogger::getInstance().tracef("PCF8574 Flash LED (P1): %s", _pcfLedState ? "ON" : "OFF");
            _lastPcfBlink = millis();
        }
    }
}
