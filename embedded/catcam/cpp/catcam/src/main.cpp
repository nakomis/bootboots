#include <Arduino.h>
#include <Wire.h>
#include <esp_camera.h>
#include <SD_MMC.h>
#include <SDLogger.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <ArduinoJson.h>

#include "BluetoothService.h"
#include "BluetoothOTA.h"
#include "PCF8574Manager.h"
#include "SystemState.h"
#include "WifiConnect.h"
#include "OTAUpdate.h"
#include "AWSAuth.h"
#include "Camera.h"
#include "CatCamHttpClient.h"
#include "version.h"
#include "secrets.h"
#include <HTTPClient.h>

#ifdef ESP32S3_CAM
#include <Adafruit_NeoPixel.h>
#define RGB_LED_PIN 48
#define NUM_LEDS 1
Adafruit_NeoPixel rgbLed(NUM_LEDS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

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

// BOOT button for user input (can exit photo loop early)
#define BOOT_BUTTON_PIN 0       // GPIO0 - BOOT button (LOW when pressed)

// Global flag to exit photo loop
volatile bool exitPhotoLoop = false;

// Global system components
SDLogger* sdLogger = nullptr;
WifiConnect* wifiConnect = nullptr;
BootBootsBluetoothService* bluetoothService = nullptr;
BluetoothOTA* bluetoothOTA = nullptr;
OTAUpdate* otaUpdate = nullptr;
PCF8574Manager* pcfManager = nullptr;
AWSAuth* awsAuth = nullptr;
Camera* camera = nullptr;

// AWS Auth configuration
const char* AWS_ROLE_ALIAS = "BootBootsRoleAlias";
const char* API_HOST = "api.bootboots.sandbox.nakomis.com";
const char* API_PATH = "/infer";

// Function declarations
void capturePhotoLoop();
void setLedColor(uint8_t r, uint8_t g, uint8_t b);
void ledOff();
bool flashLedAccelerating(uint8_t r, uint8_t g, uint8_t b, unsigned long startInterval, unsigned long endInterval, unsigned long durationMs);
bool isBootButtonPressed();

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
            }
            else {
                // Serial.printf("[CATCAM] WARNING: Failed to set boot partition: %s\n", esp_err_to_name(err));
            }
        }
    }
    else {
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

    // Initialize BOOT button for user input (exit photo loop)
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    SDLogger::getInstance().infof("BOOT button initialized on GPIO%d (press to exit photo loop)", BOOT_BUTTON_PIN);

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
    }
    else {
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
    }
    else {
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

        // Initialize AWS Auth
        awsAuth = new AWSAuth("eu-west-2");
        if (awsAuth->initialize(AWS_CERT_CA, AWS_CERT_CRT, AWS_CERT_PRIVATE, AWS_IOT_CREDENTIALS_ENDPOINT)) {
            SDLogger::getInstance().infof("AWS Auth initialized successfully");
            // Start the photo capture loop
            capturePhotoLoop();
        }
        else {
            SDLogger::getInstance().errorf("Failed to initialize AWS Auth");
        }
    }
    else {
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
    }
    else {
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
    }
    else if (!systemState.wifiConnected && WiFi.status() == WL_CONNECTED) {
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

// RGB LED Helper Functions
void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
#ifdef ESP32S3_CAM
    rgbLed.setPixelColor(0, rgbLed.Color(r, g, b));
    rgbLed.show();
#endif
}

void ledOff() {
    setLedColor(0, 0, 0);
}

// Check if BOOT button is pressed (returns true if pressed)
bool isBootButtonPressed() {
    return digitalRead(BOOT_BUTTON_PIN) == LOW;
}

// Flash LED with accelerating frequency over the duration
// Returns true if BOOT button was pressed (exit requested)
bool flashLedAccelerating(uint8_t r, uint8_t g, uint8_t b, unsigned long startInterval, unsigned long endInterval, unsigned long durationMs) {
    unsigned long startTime = millis();
    unsigned long elapsed = 0;
    bool ledOn = false;
    int toggleCount = 0;

    SDLogger::getInstance().debugf("Flash LED: color=(%d,%d,%d) start=%lu end=%lu duration=%lu",
        r, g, b, startInterval, endInterval, durationMs);

    unsigned long lastToggle = startTime;

    while (elapsed < durationMs) {
        elapsed = millis() - startTime;

        // Check for BOOT button press to exit early
        if (isBootButtonPressed()) {
            ledOff();
            exitPhotoLoop = true;
            SDLogger::getInstance().infof("BOOT button pressed - exiting photo loop");
            return true;
        }

        // Calculate current interval - interpolate from startInterval to endInterval
        float progress = (float)elapsed / (float)durationMs;
        unsigned long currentInterval = startInterval + (unsigned long)(progress * (float)(endInterval - startInterval));

        // Clamp interval to reasonable values
        if (currentInterval < 30) currentInterval = 30;
        if (currentInterval > 1000) currentInterval = 1000;

        if (millis() - lastToggle >= currentInterval) {
            ledOn = !ledOn;
            toggleCount++;
            if (ledOn) {
                setLedColor(r, g, b);
            }
            else {
                ledOff();
            }
            lastToggle = millis();
        }

        // Handle Bluetooth during the flash sequence
        if (bluetoothService) {
            bluetoothService->handle();
        }
        if (bluetoothOTA) {
            bluetoothOTA->handle();
        }

        delay(10);  // Small delay to prevent tight loop
    }

    SDLogger::getInstance().debugf("Flash complete: %d toggles", toggleCount);
    ledOff();
    return false;
}

void capturePhotoLoop() {
    SDLogger::getInstance().infof("=== Starting Photo Capture Loop (20 iterations) ===");

#ifdef ESP32S3_CAM
    // Initialize RGB LED
    SDLogger::getInstance().infof("Initializing RGB LED on GPIO %d", RGB_LED_PIN);
    rgbLed.begin();
    rgbLed.setBrightness(255);  // Full brightness for testing
    rgbLed.show();

    // LED test sequence - verify hardware works
    SDLogger::getInstance().infof("LED test: RED");
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));
    rgbLed.show();
    delay(500);

    SDLogger::getInstance().infof("LED test: GREEN");
    rgbLed.setPixelColor(0, rgbLed.Color(0, 255, 0));
    rgbLed.show();
    delay(500);

    SDLogger::getInstance().infof("LED test: BLUE");
    rgbLed.setPixelColor(0, rgbLed.Color(0, 0, 255));
    rgbLed.show();
    delay(500);

    SDLogger::getInstance().infof("LED test: OFF");
    rgbLed.setPixelColor(0, 0);
    rgbLed.show();
    delay(250);

    // Set working brightness
    rgbLed.setBrightness(100);
#endif

    // Initialize camera once for all iterations
    camera = new Camera();
    camera->init();
    delay(500);  // Give camera time to stabilize

    // Reset exit flag at start of loop
    exitPhotoLoop = false;

    for (int iteration = 1; iteration <= 20; iteration++) {
        // Check if exit was requested
        if (exitPhotoLoop) {
            SDLogger::getInstance().infof("Exiting photo loop early (after %d photos)", iteration - 1);
            break;
        }

        SDLogger::getInstance().infof("=== Photo %d of 20 (press BOOT to exit) ===", iteration);

        // Step 1: Flash red LED (accelerating) - check for button press
        SDLogger::getInstance().debugf("Red LED countdown...");
        if (flashLedAccelerating(255, 0, 0, 50, 250, 2500)) {
            break;  // Button was pressed
        }

        // Step 2: Flash blue LED (accelerating) - check for button press
        SDLogger::getInstance().debugf("Blue LED countdown...");
        if (flashLedAccelerating(0, 0, 255, 250, 500, 2500)) {
            break;  // Button was pressed
        }

        // Step 3: Set green LED on for photo capture
#ifdef ESP32S3_CAM
        rgbLed.setBrightness(100);  // 0-255, moderate brightness
#endif
        setLedColor(255, 255, 255);

        // Capture image
        NamedImage* image = camera->getImage();
        if (!image || !image->image || image->size == 0) {
            SDLogger::getInstance().errorf("Failed to capture image");
            ledOff();
            continue;
        }

#ifdef ESP32S3_CAM
        rgbLed.setBrightness(50);  // 0-255, moderate brightness
#endif
        setLedColor(0, 255, 0);

        SDLogger::getInstance().infof("Captured image: %s (%d bytes)", image->filename.c_str(), image->size);

        // Get AWS credentials (refresh if needed)
        if (!awsAuth->areCredentialsValid()) {
            SDLogger::getInstance().infof("Refreshing AWS credentials...");
            if (!awsAuth->getCredentialsWithRoleAlias(AWS_ROLE_ALIAS)) {
                SDLogger::getInstance().errorf("Failed to get AWS credentials");
                ledOff();
                continue;
            }
        }

        // Post image to inference endpoint
        CatCamHttpClient httpClient;
        String response = httpClient.postImage(image, API_HOST, API_PATH, awsAuth);

        // Parse response and log cat percentages
        DynamicJsonDocument doc(1024);
        DeserializationError jsonError = deserializeJson(doc, response);

        if (jsonError) {
            SDLogger::getInstance().warnf("Failed to parse response JSON: %s", jsonError.c_str());
            SDLogger::getInstance().infof("Raw response: %s", response.c_str());
        }
        else {
            // Extract cat percentages from response
            // Expected format: {"boots": 0.85, "laces": 0.12, "no_cat": 0.03} or similar
            String resultLog = "Cat detection results: ";
            String winner = "";
            float winnerScore = -1.0;

            JsonObject root = doc.as<JsonObject>();
            for (JsonPair kv : root) {
                const char* catName = kv.key().c_str();
                float score = kv.value().as<float>();

                // Build log string
                char scoreStr[32];
                snprintf(scoreStr, sizeof(scoreStr), "%s=%.1f%% ", catName, score * 100.0);
                resultLog += scoreStr;

                // Track winner
                if (score > winnerScore) {
                    winnerScore = score;
                    winner = catName;
                }
            }

            if (winner.length() > 0) {
                SDLogger::getInstance().infof("%s | Winner: %s (%.1f%%)",
                    resultLog.c_str(), winner.c_str(), winnerScore * 100.0);
            }
            else {
                SDLogger::getInstance().infof("Response: %s", response.c_str());
            }
        }

        // Release image buffer
        camera->releaseImageBuffer(image);

        // Step 4: Turn LED off
        ledOff();

        SDLogger::getInstance().infof("=== Photo %d complete ===", iteration);
    }

    // Cleanup camera
    camera->deInit();
    delete camera;
    camera = nullptr;

    SDLogger::getInstance().infof("=== Photo Capture Loop Complete ===");
}
