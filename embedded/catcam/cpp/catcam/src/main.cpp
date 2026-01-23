#include <Arduino.h>
#include <SD_MMC.h>
#include <SDLogger.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "SystemState.h"
#include "SystemManager.h"
#include "LedController.h"
#include "InputManager.h"
#include "CaptureController.h"
#include "version.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "bootboots-ota-2025"
#endif

// Board-specific pin configuration
#ifdef ESP32S3_CAM
// ESP32-S3 CAM: Dedicated I2C pins (not shared with UART)
#define I2C_SDA 47               // GPIO4 - SDA
#define I2C_SCL 21               // GPIO5 - SCL
#define BOARD_NAME "ESP32-S3-CAM"
#else
// Original ESP32-CAM: I2C on UART0 pins
#define I2C_SDA 1               // GPIO1 (U0TXD) - SDA
#define I2C_SCL 3               // GPIO3 (U0RXD) - SCL
#define BOARD_NAME "ESP32-CAM"
#endif

#define PCF8574_ADDRESS 0x27    // I2C address for PCF8574

// BOOT button for user input (triggers photo capture)
#define BOOT_BUTTON_PIN 0       // GPIO0 - BOOT button (LOW when pressed)

// Image storage settings
#define IMAGES_DIR "/images"
#define MAX_IMAGES_TO_KEEP 20

// AWS Auth configuration
const char* AWS_ROLE_ALIAS = "BootBootsRoleAlias";
const char* API_HOST = "api.bootboots.sandbox.nakomis.com";
const char* API_PATH = "/infer";

// Global system components
SystemState systemState;
SystemManager systemManager;
LedController ledController;
InputManager inputManager;

// Function declarations
bool isBootButtonPressed();

// Wrapper for BluetoothService extern - delegates to CaptureController
String captureAndPostPhoto();

void setup() {
    Serial.begin(115200);
    delay(500);  // Give serial time to initialize
    Serial.println("\n\n=== CATCAM STARTING ===");
    Serial.printf("Version: %s\n", FIRMWARE_VERSION);
    Serial.flush();

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
            }
            else {
                Serial.printf("[CATCAM] WARNING: Failed to set boot partition: %s\n", esp_err_to_name(err));
            }
        }
    }
    else {
        Serial.println("[CATCAM] WARNING: Factory partition not found - bootloader won't run on reboot");
    }

    // Initialize SDLogger for normal operation (after OTA check)
    Serial.println("Initializing SDLogger...");
    Serial.flush();
    bool sdLoggerOk = SDLogger::getInstance().init("/logs");
    Serial.printf("SDLogger init result: %s\n", sdLoggerOk ? "OK" : "FAILED");
    Serial.flush();
    SDLogger::getInstance().setLogLevel(LOG_DEBUG);
    SDLogger::getInstance().infof("=== BootBoots System Starting (%s) ===", BOARD_NAME);

    // Record system start time
    systemState.systemStartTime = millis();

    // Configure SystemManager
    SystemManager::Config config = {
        .i2cSDA = I2C_SDA,
        .i2cSCL = I2C_SCL,
        .pcf8574Address = PCF8574_ADDRESS,
        .bootButtonPin = BOOT_BUTTON_PIN,
        .debounceMs = 50,
        .imagesDir = IMAGES_DIR,
        .maxImagesToKeep = MAX_IMAGES_TO_KEEP,
        .otaPassword = OTA_PASSWORD,
        .deviceName = "BootBoots-CatCam",
        .awsRegion = "eu-west-2",
        .awsRoleAlias = AWS_ROLE_ALIAS,
        .apiHost = API_HOST,
        .apiPath = API_PATH
    };

    // Initialize hardware first
    systemManager.initHardware(config, systemState, inputManager);

    // Initialize all system components
    systemManager.initComponents(config, systemState, ledController, inputManager);

    // Mark system as initialized
    systemState.initialized = true;

    SDLogger::getInstance().infof("=== BootBoots System Ready ===");

    // Log system startup
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("%s system v%s (%s) initialized successfully", PROJECT_NAME, FIRMWARE_VERSION, BUILD_TIMESTAMP);
        SDLogger::getInstance().infof("System Status - Camera: %s, WiFi: %s, I2C: %s, PCF8574: %s, Atomizer: %s",
            systemState.cameraReady ? "OK" : "FAIL",
            systemState.wifiConnected ? "OK" : "FAIL",
            systemState.i2cReady ? "OK" : "FAIL",
            systemState.pcf8574Ready ? "OK" : "FAIL",
            systemState.atomizerEnabled ? "ON" : "OFF");
    }
}

void loop() {
    // Update input manager (polls button state)
    inputManager.update();

    // Check for BOOT button press to record video
    if (inputManager.wasBootButtonJustPressed()) {
        SDLogger::getInstance().infof("BOOT button pressed - recording video");
        CaptureController* captureController = systemManager.getCaptureController();
        if (captureController) {
            captureController->recordVideo();
        }
    }

    // Handle Bluetooth, OTA, and WiFi status
    systemManager.update(systemState);

    // PCF8574 heartbeat LED
    systemManager.blinkPcfLed(systemState);

    delay(100);  // Shorter delay for responsive button handling
}

// Check if BOOT button is pressed (delegates to InputManager)
// Used as callback by CaptureController for cancel checks
bool isBootButtonPressed() {
    return inputManager.isBootButtonPressed();
}

// Wrapper function for BluetoothService extern - delegates to CaptureController
String captureAndPostPhoto() {
    CaptureController* captureController = systemManager.getCaptureController();
    if (captureController) {
        return captureController->capturePhoto();
    }
    SDLogger::getInstance().errorf("CaptureController not initialized");
    return "";
}
