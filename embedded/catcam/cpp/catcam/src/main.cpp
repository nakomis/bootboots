#include <Arduino.h>
#include <esp_camera.h>
#include <SDLogger.h>

#include "BluetoothService.h"
#include "BluetoothOTA.h"
#include "SystemState.h"
#include "WifiConnect.h"
#include "version.h"
#include "secrets.h"

#include "main.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "bootboots-ota-2025"
#endif

// Global system components
SDLogger* sdLogger = nullptr;
WifiConnect* wifiConnect = nullptr;
BootBootsBluetoothService* bluetoothService = nullptr;
OTAUpdate* otaUpdate = nullptr;

// System state instance
SystemState systemState;

void setup() {
    Serial.begin(115200);
    SDLogger::getInstance().init("/logs");
    SDLogger::getInstance().setLogLevel(LOG_DEBUG);
    LOG_I("=== BootBoots System Starting ===");
    // Initialize serial for debugging
    // Serial.begin(115200);
    // SDLogger::getInstance().init();
    // SDLogger::getInstance().infof("\n=== BootBoots System Starting ===");

    // Record system start time
    systemState.systemStartTime = millis();

    // Disable watchdog timers during initialization
    // FIXME: Why were these here?
    // disableCore0WDT();
    // disableCore1WDT();

    // Initialize hardware first
    initializeHardware();

    // Initialize all system components
    initializeComponents();

    // Perform comprehensive system checks
    // FIXME: Uncomment when implemented
    // performSystemChecks();

    // Mark system as initialized
    systemState.initialized = true;

    SDLogger::getInstance().infof("=== BootBoots System Ready ===");
    SDLogger::getInstance().infof("%s", BANNER);

    // Signal successful initialization
    // FIXME: Uncomment when implemented
    // blinkStatusLED(5, 100); // 5 quick blinks
    // delay(1000);

    // Log system startup
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("BootBoots system initialized successfully");
        SDLogger::getInstance().infof("System Status - Camera: %s, WiFi: %s, I2C: %s, Atomizer: %s",
            systemState.cameraReady ? "OK" : "FAIL",
            systemState.wifiConnected ? "OK" : "FAIL",
            systemState.i2cReady ? "OK" : "FAIL",
            systemState.atomizerEnabled ? "ON" : "OFF");
    }
}

void loop() {
    SDLogger::getInstance().debug("Loop iteration");
    bluetoothService->updateSystemStatus(systemState);
    delay(1000);
}

void initializeHardware() {
    SDLogger::getInstance().infof("Initializing hardware...");


    SDLogger::getInstance().infof("Hardware initialization complete");
}

void initializeComponents() {
    SDLogger::getInstance().infof("Initializing system components...");

    // Initialize SD Logger first for early logging capability
    if (SDLogger::getInstance().init()) {
        systemState.sdCardReady = true;
        SDLogger::getInstance().infof("SD Logger initialized successfully");
    }
    else {
        systemState.sdCardReady = false;
        // FIXME: Uncomment when implemented
        // handleSystemError("SD_INIT", "Failed to initialize SD Logger");
    }

    // Initialize WiFi
    wifiConnect = new WifiConnect();
    if (wifiConnect->connect()) {
        systemState.wifiConnected = true;
        SDLogger::getInstance().infof("WiFi connected successfully");
        SDLogger::getInstance().infof("IP Address: %s", WiFi.localIP().toString().c_str());
        if (systemState.sdCardReady) {
            SDLogger::getInstance().infof("WiFi connected successfully");
        }
    } else {
        SDLogger::getInstance().warnf("WARNING: WiFi connection failed");
        systemState.wifiConnected = false;
    }

    // Initialize Bluetooth Service
    bluetoothService = new BootBootsBluetoothService();
    bluetoothService->init("BootBoots-CatCam");
    SDLogger::getInstance().infof("Bluetooth Service initialized");

    // Initialize OTA Update Service
    otaUpdate = new OTAUpdate();
    otaUpdate->init("BootBoots-CatCam", OTA_PASSWORD);
    otaUpdate->setUpdateCallback([](bool success, const char* error) {
        if (success) {
            SDLogger::getInstance().infof("OTA Update: %s", error);
        }
        else {
            SDLogger::getInstance().errorf("OTA Update failed: %s", error);
        }
        });
    SDLogger::getInstance().infof("OTA Update service initialized");
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("OTA service initialized - updates available via WiFi");
    }

}

void updateSystemStatus() {
    // Update WiFi connection status
    if (systemState.wifiConnected && WiFi.status() != WL_CONNECTED) {
        systemState.wifiConnected = false;
        SDLogger::getInstance().warnf("WARNING: WiFi connection lost");
        if (systemState.sdCardReady) {
            SDLogger::getInstance().warnf("WiFi connection lost");
        }
    } else if (!systemState.wifiConnected && WiFi.status() == WL_CONNECTED) {
        systemState.wifiConnected = true;
        SDLogger::getInstance().infof("WiFi connection restored");
        if (systemState.sdCardReady) {
            SDLogger::getInstance().infof("WiFi connection restored");
        }
    }

    // FIXME: Uncomment when implemented
    // // Update status LED
    // if (systemState.wifiConnected) {
    //     setStatusLED(false); // LED off when everything is good
    // } else {
    //     // Slow blink when offline
    //     static unsigned long lastBlink = 0;
    //     if (millis() - lastBlink > 2000) {
    //         static bool ledState = false;
    //         setStatusLED(ledState);
    //         ledState = !ledState;
    //         lastBlink = millis();
    //     }
    // }
}

void handleSystemError(const char* component, const char* error) {
    SDLogger::getInstance().errorf("SYSTEM ERROR in %s: %s", component, error);

    // Log error to SD card if available
    if (systemState.sdCardReady) {
        char errorMessage[128];
        snprintf(errorMessage, sizeof(errorMessage), "ERROR in %s: %s", component, error);
        SDLogger::getInstance().errorf("%s", errorMessage);
    }

    // Visual error indication
    // FIXME: Uncomment when implemented
    // blinkStatusLED(5, 200); // 5 fast blinks for error
}


