#include <Arduino.h>
#include <Wire.h>
#include <esp_camera.h>
#include <SD_MMC.h>
#include <SDLogger.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <ArduinoJson.h>
#include <time.h>

#include "BluetoothService.h"
#include "BluetoothOTA.h"
#include "PCF8574Manager.h"
#include "SystemState.h"
#include "WifiConnect.h"
#include "OTAUpdate.h"
#include "AWSAuth.h"
#include "Camera.h"
#include "CatCamHttpClient.h"
#include "VideoRecorder.h"
#include "ImageStorage.h"
#include "LedController.h"
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

// Global system components
SDLogger* sdLogger = nullptr;
WifiConnect* wifiConnect = nullptr;
BootBootsBluetoothService* bluetoothService = nullptr;
BluetoothOTA* bluetoothOTA = nullptr;
OTAUpdate* otaUpdate = nullptr;
PCF8574Manager* pcfManager = nullptr;
AWSAuth* awsAuth = nullptr;
Camera* camera = nullptr;
VideoRecorder* videoRecorder = nullptr;
ImageStorage* imageStorage = nullptr;
LedController ledController;

// AWS Auth configuration
const char* AWS_ROLE_ALIAS = "BootBootsRoleAlias";
const char* API_HOST = "api.bootboots.sandbox.nakomis.com";
const char* API_PATH = "/infer";

// Function declarations
void initCameraAndLed();
String captureAndPostPhoto();
void recordVideo();
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
    // Check for BOOT button press to record video
    static bool lastButtonState = false;
    bool buttonPressed = isBootButtonPressed();

    // Detect rising edge (button just pressed) with debounce
    if (buttonPressed && !lastButtonState) {
        delay(50);  // Debounce
        if (isBootButtonPressed()) {  // Confirm still pressed
            SDLogger::getInstance().infof("BOOT button pressed - recording video");
            recordVideo();
        }
    }
    lastButtonState = buttonPressed;

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

    delay(100);  // Shorter delay for responsive button handling
}

void initializeHardware() {
    SDLogger::getInstance().infof("Initializing hardware...");

    // Initialize BOOT button for user input (triggers photo capture)
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    SDLogger::getInstance().infof("BOOT button initialized on GPIO%d (press to capture photo)", BOOT_BUTTON_PIN);

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
        // Initialize image storage for captured photos
        imageStorage = new ImageStorage();
        if (imageStorage->init(IMAGES_DIR, MAX_IMAGES_TO_KEEP)) {
            SDLogger::getInstance().infof("Image storage initialized");
        } else {
            SDLogger::getInstance().warnf("Image storage initialization failed");
        }
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
            // Initialize camera and LED for photo capture
            initCameraAndLed();
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
    bluetoothService->setLedController(&ledController);
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

// Check if BOOT button is pressed (returns true if pressed)
bool isBootButtonPressed() {
    return digitalRead(BOOT_BUTTON_PIN) == LOW;
}

void initCameraAndLed() {
    SDLogger::getInstance().infof("=== Initializing Camera and LED ===");

    // Initialize LED controller and run test sequence
    ledController.init(100);  // Default brightness 100
    ledController.runTestSequence(3, 100);

    // Initialize camera
    camera = new Camera();
    camera->init();
    delay(500);  // Give camera time to stabilize

    // Initialize video recorder
    videoRecorder = new VideoRecorder();
    if (videoRecorder->init()) {
        SDLogger::getInstance().infof("Video Recorder initialized successfully");
    } else {
        SDLogger::getInstance().warnf("Video Recorder initialization failed");
    }

    SDLogger::getInstance().infof("=== Camera and LED Ready - Press BOOT to record video ===");
}

String captureAndPostPhoto() {
    if (!camera) {
        SDLogger::getInstance().errorf("Camera not initialized");
        return "";
    }

    SDLogger::getInstance().infof("=== Capturing Photo ===");

    // Callbacks for LED animations
    auto cancelCheck = []() { return isBootButtonPressed(); };
    auto loopCallback = []() {
        if (bluetoothService) bluetoothService->handle();
        if (bluetoothOTA) bluetoothOTA->handle();
    };

    // Step 1: Flash red LED (starts slow, gets faster)
    SDLogger::getInstance().debugf("Red LED countdown...");
    ledController.flashAccelerating(255, 0, 0, 500, 250, 2500, cancelCheck, loopCallback);

    // Step 2: Flash blue LED (starts slow, gets faster)
    SDLogger::getInstance().debugf("Blue LED countdown...");
    ledController.flashAccelerating(0, 0, 255, 250, 50, 2500, cancelCheck, loopCallback);

    // Step 3: Set bright WHITE for photo capture
    ledController.setBrightness(255);  // Maximum brightness for photo
    ledController.setColor(255, 255, 255);

    // Capture image
    NamedImage* image = camera->getImage();
    if (!image || !image->image || image->size == 0) {
        SDLogger::getInstance().errorf("Failed to capture image");
        ledController.off();
        return "";
    }

    // Generate timestamp-based filename for this capture
    String basename = imageStorage->generateFilename();

    ledController.setBrightness(50);  // Moderate brightness
    ledController.setColor(0, 255, 0);

    SDLogger::getInstance().infof("Captured image: %s (%d bytes)", basename.c_str(), image->size);

    // Save image to SD card
    imageStorage->saveImage(basename, image);

    // Get AWS credentials (refresh if needed)
    if (!awsAuth->areCredentialsValid()) {
        SDLogger::getInstance().infof("Refreshing AWS credentials...");
        if (!awsAuth->getCredentialsWithRoleAlias(AWS_ROLE_ALIAS)) {
            SDLogger::getInstance().errorf("Failed to get AWS credentials");
            camera->releaseImageBuffer(image);
            ledController.off();
            return "";
        }
    }

    // Post image to inference endpoint
    CatCamHttpClient httpClient;
    String response = httpClient.postImage(image, API_HOST, API_PATH, awsAuth);

    // Save server response to SD card
    imageStorage->saveResponse(basename, response);

    // Parse response and log cat percentages
    // Format: {"success": true, "data": {"probabilities": [...]}, "mostLikelyCat": {"name": "Tau", "confidence": 0.71, "index": 4}}
    DynamicJsonDocument doc(2048);
    DeserializationError jsonError = deserializeJson(doc, response);

    // Cat names matching model output indices
    const char* CAT_NAMES[] = { "Boots", "Chi", "Kappa", "Mu", "Tau", "NoCat" };

    if (jsonError) {
        SDLogger::getInstance().warnf("Failed to parse response JSON: %s", jsonError.c_str());
        SDLogger::getInstance().infof("Raw response: %s", response.c_str());
    }
    else if (doc["success"] == true) {
        // Get winner from mostLikelyCat object at root level
        JsonObject mostLikelyCat = doc["mostLikelyCat"];
        const char* winnerName = mostLikelyCat["name"] | "Unknown";
        float winnerConfidence = mostLikelyCat["confidence"] | 0.0f;

        // Build results string from probabilities array in data
        String resultLog = "";
        JsonObject data = doc["data"];
        if (data.containsKey("probabilities") && data["probabilities"].is<JsonArray>()) {
            JsonArray probabilities = data["probabilities"];
            for (size_t i = 0; i < probabilities.size() && i < 6; i++) {
                float score = probabilities[i].as<float>();
                char scoreStr[32];
                snprintf(scoreStr, sizeof(scoreStr), "%s=%.1f%% ", CAT_NAMES[i], score * 100.0);
                resultLog += scoreStr;
            }
        }

        SDLogger::getInstance().infof("%s | Winner: %s (%.1f%%)",
            resultLog.c_str(), winnerName, winnerConfidence * 100.0);
    }
    else {
        // Log the raw response if format is unexpected
        SDLogger::getInstance().warnf("Unexpected response format: %s", response.c_str());
    }

    // Release image buffer
    camera->releaseImageBuffer(image);

    // Clean up old images (keep only the most recent MAX_IMAGES_TO_KEEP)
    imageStorage->cleanupOldImages();

    // Step 4: Turn LED off
    ledController.off();

    SDLogger::getInstance().infof("=== Photo Capture Complete ===");

    // Return the filename (with .jpg extension)
    return basename + ".jpg";
}

void recordVideo() {
    if (!videoRecorder) {
        SDLogger::getInstance().errorf("Video recorder not initialized");
        return;
    }

    if (videoRecorder->isRecording()) {
        SDLogger::getInstance().warnf("Video recording already in progress");
        return;
    }

    SDLogger::getInstance().infof("=== Starting Video Recording ===");

    // Callbacks for LED animations
    auto cancelCheck = []() { return isBootButtonPressed(); };
    auto loopCallback = []() {
        if (bluetoothService) bluetoothService->handle();
        if (bluetoothOTA) bluetoothOTA->handle();
    };

    // Step 1: Flash red LED (starts slow, gets faster) - same as photo capture
    SDLogger::getInstance().debugf("Red LED countdown...");
    ledController.flashAccelerating(255, 0, 0, 500, 250, 2500, cancelCheck, loopCallback);

    // Step 2: Flash blue LED (starts slow, gets faster) - same as photo capture
    SDLogger::getInstance().debugf("Blue LED countdown...");
    ledController.flashAccelerating(0, 0, 255, 250, 50, 2500, cancelCheck, loopCallback);

    // Step 3: Set WHITE LED on during recording
    ledController.setBrightness(255);  // Maximum brightness for recording
    ledController.setColor(255, 255, 255);

    // Configure and start recording
    VideoConfig config = VideoRecorder::getDefaultConfig();
    config.frameSize = FRAMESIZE_VGA;      // 640x480 for good balance
    config.quality = 12;                    // Good quality
    config.fps = 10;                        // 10 frames per second
    config.durationSeconds = 10;            // 10 second video
    config.outputDir = "/videos";

    SDLogger::getInstance().infof("Recording %d seconds of video at %d fps...",
        config.durationSeconds, config.fps);

    // Record with progress callback to update LED
    VideoResult result = videoRecorder->recordWithProgress(config,
        [](uint32_t currentFrame, uint32_t totalFrames, uint32_t elapsedMs) {
            // Keep LED white during recording, pulse brightness every second
            static uint32_t lastSecond = 0;
            uint32_t currentSecond = elapsedMs / 1000;
            if (currentSecond != lastSecond) {
                lastSecond = currentSecond;
                SDLogger::getInstance().debugf("Recording: frame %d/%d (%.1fs)",
                    currentFrame, totalFrames, elapsedMs / 1000.0f);
            }
        }
    );

    // Step 4: Turn LED off after recording and restore normal brightness
    ledController.off();
    ledController.setBrightness(100);

    if (result.success) {
        ledController.flashSuccess();
        SDLogger::getInstance().infof("=== Video Recording Complete ===");
        SDLogger::getInstance().infof("Saved: %s (%d frames, %d bytes, %d ms)",
            result.filename.c_str(), result.totalFrames, result.fileSize, result.durationMs);
    } else {
        ledController.flashError();
        SDLogger::getInstance().errorf("=== Video Recording Failed ===");
        SDLogger::getInstance().errorf("Error: %s", result.errorMessage.c_str());
    }
}
