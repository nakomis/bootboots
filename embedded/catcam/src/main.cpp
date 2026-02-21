#include <Arduino.h>
#include <SD_MMC.h>
#include <SDLogger.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <Preferences.h>

#include "SystemState.h"
#include "SystemManager.h"
#include "PCF8574Manager.h"
#include "LedController.h"
#include "InputManager.h"
#include "CaptureController.h"
#include "MotionDetector.h"
#include "DeterrentController.h"
#include "BluetoothService.h"
#include "CommandDispatcher.h"
#include "Camera.h"
#include "version.h"
#include "secrets.h"

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
Preferences preferences;

// Function declarations
bool isBootButtonPressed();
void saveTrainingMode(bool enabled);
void loadCameraSettings();
void saveCameraSetting(const String& setting, int value);

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

    // Load training mode and deterrent settings from NVS
    preferences.begin("bootboots", false);
    systemState.trainingMode = preferences.getBool("trainingMode", false);
    systemState.triggerThresh = preferences.getFloat("triggerThresh", 0.80f);
    systemState.dryRun = preferences.getBool("dryRun", false);
    systemState.claudeInfer = preferences.getBool("claudeInfer", false);
    SDLogger::getInstance().infof("Training mode loaded from NVS: %s", systemState.trainingMode ? "ON" : "OFF");
    SDLogger::getInstance().infof("Trigger threshold loaded from NVS: %.2f", systemState.triggerThresh);
    SDLogger::getInstance().infof("Dry-run mode loaded from NVS: %s", systemState.dryRun ? "ON" : "OFF");
    SDLogger::getInstance().infof("Claude inference loaded from NVS: %s", systemState.claudeInfer ? "ON" : "OFF");
    preferences.end();

    // Load camera settings from NVS
    loadCameraSettings();

    // Configure SystemManager
    SystemManager::Config config = {
        .i2cSDA = I2C_SDA,
        .i2cSCL = I2C_SCL,
        .pcf8574Address = PCF8574_ADDRESS,
        .bootButtonPin = BOOT_BUTTON_PIN,
        .debounceMs = 50,
        .imagesDir = IMAGES_DIR,
        .maxImagesToKeep = MAX_IMAGES_TO_KEEP,
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

    // Register callbacks with CommandDispatcher (handles commands from BLE and MQTT)
    CommandDispatcher* dispatcher = systemManager.getCommandDispatcher();
    if (dispatcher) {
        dispatcher->setTrainingModeCallback(saveTrainingMode);
        dispatcher->setCameraSettingCallback(saveCameraSetting);

        // set_trigger_threshold {"value": 0.85} — update Boots confidence threshold
        dispatcher->registerHandler("set_trigger_threshold", [](CommandContext& ctx) {
            float value = ctx.request["value"] | 0.80f;
            if (value < 0.0f) value = 0.0f;
            if (value > 1.0f) value = 1.0f;
            systemState.triggerThresh = value;
            preferences.begin("bootboots", false);
            preferences.putFloat("triggerThresh", value);
            preferences.end();
            SDLogger::getInstance().infof("Trigger threshold set to %.2f (%.0f%%)", value, value * 100.0f);

            DynamicJsonDocument response(256);
            response["type"] = "setting_updated";
            response["setting"] = "trigger_threshold";
            response["value"] = value;
            String responseStr;
            serializeJson(response, responseStr);
            ctx.sender->sendResponse(responseStr);
            return true;
        });

        // set_dry_run {"enabled": true} — enable/disable dry-run mode
        dispatcher->registerHandler("set_dry_run", [](CommandContext& ctx) {
            bool value = ctx.request["enabled"] | false;
            systemState.dryRun = value;
            preferences.begin("bootboots", false);
            preferences.putBool("dryRun", value);
            preferences.end();
            SDLogger::getInstance().infof("Dry-run mode %s", value ? "ON" : "OFF");

            DynamicJsonDocument response(256);
            response["type"] = "setting_updated";
            response["setting"] = "dry_run";
            response["value"] = value;
            String responseStr;
            serializeJson(response, responseStr);
            ctx.sender->sendResponse(responseStr);
            return true;
        });

        // set_claude_infer {"enabled": true} — enable/disable parallel Claude vision inference
        dispatcher->registerHandler("set_claude_infer", [](CommandContext& ctx) {
            bool value = ctx.request["enabled"] | false;
            systemState.claudeInfer = value;
            preferences.begin("bootboots", false);
            preferences.putBool("claudeInfer", value);
            preferences.end();
            SDLogger::getInstance().infof("Claude inference %s", value ? "ON" : "OFF");

            DynamicJsonDocument response(256);
            response["type"] = "setting_updated";
            response["setting"] = "claude_infer";
            response["value"] = value;
            String responseStr;
            serializeJson(response, responseStr);
            ctx.sender->sendResponse(responseStr);
            return true;
        });
    }

    // Sync training mode to capture controller
    CaptureController* captureController = systemManager.getCaptureController();
    if (captureController) {
        captureController->setTrainingMode(systemState.trainingMode);
    }

    // Mark system as initialized
    systemState.initialized = true;

    SDLogger::getInstance().infof("=== BootBoots System Ready ===");
    SDLogger::getInstance().info(BANNER);

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

    // Check for PIR motion detection
    MotionDetector* motionDetector = systemManager.getMotionDetector();
    if (motionDetector && motionDetector->wasMotionDetected()) {
        SDLogger::getInstance().infof("PIR motion detected");
        systemState.motionTriggerCount++;

        CaptureController* captureController = systemManager.getCaptureController();
        DeterrentController* deterrentController = systemManager.getDeterrentController();

        if (captureController) {
            // Training mode: capture photo without inference/deterrent
            if (systemState.trainingMode) {
                SDLogger::getInstance().infof("Training mode: capturing photo without inference");
                captureController->captureTrainingPhoto();
            }
            // Normal mode: capture photo and run inference with deterrent
            else if (deterrentController) {
                // Capture photo and run inference
                DetectionResult result = captureController->captureAndDetect(systemState.claudeInfer);
                if (result.success && deterrentController->shouldActivate(result, systemState.triggerThresh)) {
                    SDLogger::getInstance().criticalf("Boots detected (%.1f%%) - activating deterrent! (dryRun=%s)",
                        result.confidence * 100.0f, systemState.dryRun ? "ON" : "OFF");
                    systemState.deterrentActivationCount++;
                    systemState.bootsDetections++;
                    deterrentController->activate(systemState, systemState.dryRun);  // BLOCKING ~10s
                } else if (result.success) {
                    systemState.totalDetections++;
                    if (result.detectedIndex != DeterrentController::BOOTS_INDEX) {
                        systemState.falsePositivesAvoided++;
                    }
                }
            }
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
        // Use training mode capture if training mode is enabled
        if (systemState.trainingMode) {
            return captureController->captureTrainingPhoto();
        }
        return captureController->capturePhoto();
    }
    SDLogger::getInstance().errorf("CaptureController not initialized");
    return "";
}

// Save training mode to NVS
void saveTrainingMode(bool enabled) {
    preferences.begin("bootboots", false);
    preferences.putBool("trainingMode", enabled);
    preferences.end();
    SDLogger::getInstance().infof("Training mode saved to NVS: %s", enabled ? "ON" : "OFF");

    // Also update capture controller
    CaptureController* captureController = systemManager.getCaptureController();
    if (captureController) {
        captureController->setTrainingMode(enabled);
    }
}

// Load camera settings from NVS
void loadCameraSettings() {
    CameraSettings& cs = systemState.cameraSettings;
    if (!preferences.begin("bootboots", true)) {  // read-only
        SDLogger::getInstance().errorf("Failed to open NVS namespace 'bootboots' for reading");
        return;
    }

    cs.frameSize = preferences.getInt("camFrmSize", cs.frameSize);
    cs.jpegQuality = preferences.getInt("camJpgQual", cs.jpegQuality);
    cs.fbCount = preferences.getInt("camFbCount", cs.fbCount);
    cs.brightness = preferences.getInt("camBright", cs.brightness);
    cs.contrast = preferences.getInt("camContrast", cs.contrast);
    cs.saturation = preferences.getInt("camSat", cs.saturation);
    cs.specialEffect = preferences.getInt("camEffect", cs.specialEffect);
    cs.whiteBalance = preferences.getBool("camWB", cs.whiteBalance);
    cs.awbGain = preferences.getBool("camAWBGain", cs.awbGain);
    cs.wbMode = preferences.getInt("camWBMode", cs.wbMode);
    cs.exposureCtrl = preferences.getBool("camExpCtrl", cs.exposureCtrl);
    cs.aec2 = preferences.getBool("camAEC2", cs.aec2);
    cs.aeLevel = preferences.getInt("camAELevel", cs.aeLevel);
    cs.aecValue = preferences.getInt("camAECVal", cs.aecValue);
    cs.gainCtrl = preferences.getBool("camGainCtrl", cs.gainCtrl);
    cs.agcGain = preferences.getInt("camAGCGain", cs.agcGain);
    cs.gainCeiling = preferences.getInt("camGainCeil", cs.gainCeiling);
    cs.bpc = preferences.getBool("camBPC", cs.bpc);
    cs.wpc = preferences.getBool("camWPC", cs.wpc);
    cs.rawGma = preferences.getBool("camGamma", cs.rawGma);
    cs.lenc = preferences.getBool("camLenc", cs.lenc);
    cs.hmirror = preferences.getBool("camHMirror", cs.hmirror);
    cs.vflip = preferences.getBool("camVFlip", cs.vflip);
    cs.dcw = preferences.getBool("camDCW", cs.dcw);
    cs.colorbar = preferences.getBool("camColorbar", cs.colorbar);
    cs.ledDelayMillis = preferences.getInt("ledDelayMillis", cs.ledDelayMillis);

    preferences.end();
    SDLogger::getInstance().infof("Camera settings loaded from NVS (ledDelayMillis=%d)", cs.ledDelayMillis);
}

// Save a single camera setting to NVS and apply to camera
void saveCameraSetting(const String& setting, int value) {
    CameraSettings& cs = systemState.cameraSettings;
    if (!preferences.begin("bootboots", false)) {
        SDLogger::getInstance().errorf("Failed to open NVS namespace 'bootboots' for writing");
        return;
    }

    if (setting == "frame_size") { preferences.putInt("camFrmSize", cs.frameSize); }
    else if (setting == "jpeg_quality") { preferences.putInt("camJpgQual", cs.jpegQuality); }
    else if (setting == "fb_count") { preferences.putInt("camFbCount", cs.fbCount); }
    else if (setting == "brightness") { preferences.putInt("camBright", cs.brightness); }
    else if (setting == "contrast") { preferences.putInt("camContrast", cs.contrast); }
    else if (setting == "saturation") { preferences.putInt("camSat", cs.saturation); }
    else if (setting == "special_effect") { preferences.putInt("camEffect", cs.specialEffect); }
    else if (setting == "white_balance") { preferences.putBool("camWB", cs.whiteBalance); }
    else if (setting == "awb_gain") { preferences.putBool("camAWBGain", cs.awbGain); }
    else if (setting == "wb_mode") { preferences.putInt("camWBMode", cs.wbMode); }
    else if (setting == "exposure_ctrl") { preferences.putBool("camExpCtrl", cs.exposureCtrl); }
    else if (setting == "aec2") { preferences.putBool("camAEC2", cs.aec2); }
    else if (setting == "ae_level") { preferences.putInt("camAELevel", cs.aeLevel); }
    else if (setting == "aec_value") { preferences.putInt("camAECVal", cs.aecValue); }
    else if (setting == "gain_ctrl") { preferences.putBool("camGainCtrl", cs.gainCtrl); }
    else if (setting == "agc_gain") { preferences.putInt("camAGCGain", cs.agcGain); }
    else if (setting == "gain_ceiling") { preferences.putInt("camGainCeil", cs.gainCeiling); }
    else if (setting == "bpc") { preferences.putBool("camBPC", cs.bpc); }
    else if (setting == "wpc") { preferences.putBool("camWPC", cs.wpc); }
    else if (setting == "raw_gma") { preferences.putBool("camGamma", cs.rawGma); }
    else if (setting == "lenc") { preferences.putBool("camLenc", cs.lenc); }
    else if (setting == "hmirror") { preferences.putBool("camHMirror", cs.hmirror); }
    else if (setting == "vflip") { preferences.putBool("camVFlip", cs.vflip); }
    else if (setting == "dcw") { preferences.putBool("camDCW", cs.dcw); }
    else if (setting == "colorbar") { preferences.putBool("camColorbar", cs.colorbar); }
    else if (setting == "led_delay_millis") {
        SDLogger::getInstance().infof("Saving ledDelayMillis=%d to NVS", cs.ledDelayMillis);
        size_t written = preferences.putInt("ledDelayMillis", cs.ledDelayMillis);
        if (written == 0) {
            SDLogger::getInstance().errorf("Failed to write ledDelayMillis to NVS");
        }
    }

    preferences.end();

    // Apply updated settings to camera
    Camera* camera = systemManager.getCamera();
    if (camera) {
        camera->applySettings(cs);
    }

    SDLogger::getInstance().infof("Camera setting '%s' saved to NVS and applied", setting.c_str());
}
