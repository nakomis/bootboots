#include <Arduino.h>
#include <Wire.h>
#include <esp_camera.h>
#include <SD_MMC.h>
#include <SDLogger.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "BluetoothService.h"
#include "BluetoothOTA.h"
#include "PCF8574Manager.h"
#include "SystemState.h"
#include "WifiConnect.h"
#include "OTAUpdate.h"
#include "AWSAuth.h"
#include "version.h"
#include "secrets.h"
#include <HTTPClient.h>

#include "main.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "bootboots-ota-2025"
#endif

// Board-specific pin configuration
#ifdef ESP32S3_CAM
// ESP32-S3 CAM: Dedicated I2C pins (not shared with UART)
#define I2C_SDA 4               // GPIO4 - SDA
#define I2C_SCL 5               // GPIO5 - SCL
#define BOARD_NAME "ESP32-S3-CAM"
#else
// Original ESP32-CAM: I2C on UART0 pins
#define I2C_SDA 1               // GPIO1 (U0TXD) - SDA
#define I2C_SCL 3               // GPIO3 (U0RXD) - SCL
#define BOARD_NAME "ESP32-CAM"
#endif

#define PCF8574_ADDRESS 0x20    // I2C address for PCF8574

// Global system components
SDLogger* sdLogger = nullptr;
WifiConnect* wifiConnect = nullptr;
BootBootsBluetoothService* bluetoothService = nullptr;
BluetoothOTA* bluetoothOTA = nullptr;
OTAUpdate* otaUpdate = nullptr;
PCF8574Manager* pcfManager = nullptr;
AWSAuth* awsAuth = nullptr;

// AWS Auth configuration
const char* AWS_ROLE_ALIAS = "BootBootsRoleAlias";
const char* API_HOST = "api.bootboots.sandbox.nakomis.com";
const char* API_PATH = "/authCheck";

// Function declarations
void testAWSAuth();

// System state instance
SystemState systemState;

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
                // Serial.println("[CATCAM] Set boot partition to factory for next reboot");
            } else {
                // Serial.printf("[CATCAM] WARNING: Failed to set boot partition: %s\n", esp_err_to_name(err));
            }
        }
    } else {
        // Serial.println("[CATCAM] WARNING: Factory partition not found - bootloader won't run on reboot");
    }

    // Direct HTTP OTA is now used instead of two-stage SD card approach
    // No need to check for pending OTA updates on boot

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
        SDLogger::getInstance().infof("System Status - Camera: %s, WiFi: %s, I2C: %s, PCF8574: %s, Atomizer: %s",
            systemState.cameraReady ? "OK" : "FAIL",
            systemState.wifiConnected ? "OK" : "FAIL",
            systemState.i2cReady ? "OK" : "FAIL",
            systemState.pcf8574Ready ? "OK" : "FAIL",
            systemState.atomizerEnabled ? "ON" : "OFF");
    }
}

void loop() {

    // Handle Bluetooth service (deferred operations)
    if (bluetoothService) {
        bluetoothService->handle();
    }

    if (bluetoothOTA) {
        bluetoothOTA->handle();
    }

    // Handle OTA updates
    if (otaUpdate) {
        otaUpdate->handle();
    }

    // PCF8574 Test: Blink LED on P1 (Flash LED pin) every 2 seconds if PCF8574 is ready
    if (pcfManager && systemState.pcf8574Ready) {
        static unsigned long lastBlink = 0;
        static bool ledState = false;

        if (millis() - lastBlink > 2000) {
            ledState = !ledState;
            pcfManager->setFlashLED(ledState);
            SDLogger::getInstance().debugf("PCF8574 Flash LED (P1): %s", ledState ? "ON" : "OFF");
            lastBlink = millis();
        }
    }

    delay(1000);
}

void initializeHardware() {
    SDLogger::getInstance().infof("Initializing hardware...");

    // Enable internal pull-ups on I2C pins before initializing I2C
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);

    // Initialize I2C with custom pins (UART0 pins for PCF8574)
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 100kHz I2C clock for reliability
    systemState.i2cReady = true;
    SDLogger::getInstance().infof("I2C initialized on GPIO%d (SDA) and GPIO%d (SCL) with internal pull-ups", I2C_SDA, I2C_SCL);

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

    // Initialize PCF8574 Manager
    pcfManager = new PCF8574Manager(PCF8574_ADDRESS);
    if (pcfManager->init(I2C_SDA, I2C_SCL)) {
        systemState.pcf8574Ready = true;
        SDLogger::getInstance().infof("PCF8574 Manager initialized successfully at address 0x%02X", PCF8574_ADDRESS);
        if (systemState.sdCardReady) {
            SDLogger::getInstance().infof("PCF8574 Manager initialized - 8 GPIO pins available");
        }
    } else {
        systemState.pcf8574Ready = false;
        SDLogger::getInstance().warnf("WARNING: PCF8574 Manager initialization failed");
        SDLogger::getInstance().warnf("Check I2C connections on GPIO%d (SDA) and GPIO%d (SCL)", I2C_SDA, I2C_SCL);
    }

    // Initialize WiFi
    wifiConnect = new WifiConnect();
    if (wifiConnect->connect() == 0) {  // connect() returns 0 on success
        systemState.wifiConnected = true;
        SDLogger::getInstance().infof("WiFi connected successfully");
        SDLogger::getInstance().infof("IP Address: %s", WiFi.localIP().toString().c_str());
        if (systemState.sdCardReady) {
            SDLogger::getInstance().infof("WiFi connected successfully");
        }

        // Initialize AWS Auth and test it
        awsAuth = new AWSAuth("eu-west-2");
        if (awsAuth->initialize(AWS_CERT_CA, AWS_CERT_CRT, AWS_CERT_PRIVATE, AWS_IOT_CREDENTIALS_ENDPOINT)) {
            SDLogger::getInstance().infof("AWS Auth initialized successfully");
            // Run AWS Auth test
            testAWSAuth();
        } else {
            SDLogger::getInstance().errorf("Failed to initialize AWS Auth");
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

void testAWSAuth() {
    SDLogger::getInstance().infof("=== Testing AWS Auth ===");

    // Step 1: Get temporary credentials using the role alias
    SDLogger::getInstance().infof("Step 1: Getting temporary credentials via role alias: %s", AWS_ROLE_ALIAS);
    if (!awsAuth->getCredentialsWithRoleAlias(AWS_ROLE_ALIAS)) {
        SDLogger::getInstance().errorf("Failed to get AWS credentials");
        return;
    }

    SDLogger::getInstance().infof("Credentials obtained successfully!");

    // Step 2: Make a signed POST request to authCheck endpoint
    SDLogger::getInstance().infof("Step 2: Making signed POST request to https://%s%s", API_HOST, API_PATH);

    String payload = "{}";  // Empty JSON payload

    // Create SigV4 signed headers
    SigV4Headers headers = awsAuth->createSigV4Headers("POST", API_PATH, API_HOST, payload);

    if (!headers.isValid) {
        SDLogger::getInstance().errorf("Failed to create SigV4 headers");
        return;
    }

    // Make the actual HTTP request
    WiFiClientSecure client;
    client.setInsecure();  // For testing - in production, set proper CA cert

    HTTPClient http;
    String url = "https://" + String(API_HOST) + String(API_PATH);
    http.begin(client, url);

    // Add all required headers
    http.addHeader("Content-Type", headers.contentType);
    http.addHeader("Host", headers.host);
    http.addHeader("X-Amz-Date", headers.date);
    http.addHeader("X-Amz-Security-Token", headers.securityToken);
    http.addHeader("Authorization", headers.authorization);

    SDLogger::getInstance().debugf("Authorization: %s", headers.authorization.c_str());

    int httpResponseCode = http.POST(payload);

    SDLogger::getInstance().infof("HTTP Response Code: %d", httpResponseCode);

    if (httpResponseCode == 200) {
        SDLogger::getInstance().infof("SUCCESS! authCheck returned 200");
        String response = http.getString();
        SDLogger::getInstance().infof("Response: %s", response.c_str());
    } else {
        SDLogger::getInstance().errorf("FAILED! Expected 200, got %d", httpResponseCode);
        String response = http.getString();
        SDLogger::getInstance().errorf("Response: %s", response.c_str());
    }

    http.end();

    SDLogger::getInstance().infof("=== AWS Auth Test Complete ===");
}
