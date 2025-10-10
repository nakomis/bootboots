#include <Arduino.h>
#include <esp_camera.h>
#include <SD_MMC.h>
#include <SDLogger.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "BluetoothService.h"
#include "BluetoothOTA.h"
#include "SystemState.h"
#include "WifiConnect.h"
#include "OTAUpdate.h"
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
BluetoothOTA* bluetoothOTA = nullptr;
OTAUpdate* otaUpdate = nullptr;

// System state instance
SystemState systemState;

void setup() {
    Serial.begin(115200);
    delay(100);

    // CRITICAL: Set boot partition to factory so bootloader runs on next reboot
    // This ensures the bootloader can check for pending OTA updates from SD card
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY,
        NULL
    );

    if (factory != NULL) {
        const esp_partition_t* boot_partition = esp_ota_get_boot_partition();

        // Only set boot partition if it's not already pointing to factory
        if (boot_partition != factory) {
            esp_err_t err = esp_ota_set_boot_partition(factory);
            if (err == ESP_OK) {
                Serial.println("[CATCAM] Set boot partition to factory for next reboot");
            } else {
                Serial.printf("[CATCAM] WARNING: Failed to set boot partition: %s\n", esp_err_to_name(err));
            }
        }
    } else {
        Serial.println("[CATCAM] WARNING: Factory partition not found - bootloader won't run on reboot");
    }

    // Direct HTTP OTA is now used instead of two-stage SD card approach
    // No need to check for pending OTA updates on boot

    // Initialize SDLogger for normal operation (after OTA check)
    SDLogger::getInstance().init("/logs");
    SDLogger::getInstance().setLogLevel(LOG_DEBUG);
    LOG_I("=== BootBoots System Starting ===");

    // Record system start time
    systemState.systemStartTime = millis();

    // Initialize hardware first
    initializeHardware();

    // Initialize all system components
    initializeComponents();

    // Mark system as initialized
    systemState.initialized = true;

    SDLogger::getInstance().infof("=== BootBoots System Ready ===");
    SDLogger::getInstance().infof("%s", BANNER);

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
    // Update Bluetooth services - only every 10 seconds to avoid saturating BLE bandwidth
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 10000) {
        bluetoothService->updateSystemStatus(systemState);
        lastStatusUpdate = millis();
    }

    if (bluetoothOTA) {
        bluetoothOTA->handle();
    }

    // Handle OTA updates
    if (otaUpdate) {
        otaUpdate->handle();
    }

    delay(1000);
}

void initializeHardware() {
    SDLogger::getInstance().infof("Initializing hardware...");


    SDLogger::getInstance().infof("Hardware initialization complete");
}

void initializeComponents() {
    SDLogger::getInstance().infof("Initializing system components...");

    // Check if SDLogger is initialized
    if (SDLogger::getInstance().isInitialized()) {
        systemState.sdCardReady = true;
        SDLogger::getInstance().infof("SD Logger initialized successfully");
    } else {
        systemState.sdCardReady = false;
        // FIXME: Uncomment when implemented
        // handleSystemError("SD_INIT", "Failed to initialize SD Logger");
    }

    // NOTE: OTA flash check has been moved to setup() to run BEFORE SDLogger init
    // This prevents flash access conflicts during firmware updates

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

    // Initialize Bluetooth OTA Service using the shared BLE server
    bluetoothOTA = new BluetoothOTA();
    if (bluetoothOTA->initWithExistingServer(bluetoothService->getServer())) {
        bluetoothOTA->setOTAUpdate(otaUpdate);
        SDLogger::getInstance().infof("Bluetooth OTA service initialized with shared BLE server");
        if (systemState.sdCardReady) {
            SDLogger::getInstance().infof("Bluetooth OTA enabled - remote updates via web interface");
        }
    } else {
        SDLogger::getInstance().errorf("Failed to initialize Bluetooth OTA service");
        delete bluetoothOTA;
        bluetoothOTA = nullptr;
    }

    // Start BLE advertising
    // This must be done after all services are initialized
    BLEDevice::startAdvertising();
    SDLogger::getInstance().infof("BLE advertising started");
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


