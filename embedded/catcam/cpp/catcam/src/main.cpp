/**
 * BootBoots Autonomous Cat Territory Management System
 * ESP32-CAM with AI-powered cat recognition and deterrent mechanisms
 * 
 * Features:
 * - AI-powered cat recognition (6 cats with special Kappa protection)
 * - Ultra-safe deterrent activation with multiple validation layers
 * - Comprehensive SD card logging
 * - WiFi connectivity for remote monitoring
 * - I2C expansion for additional sensors/controls
 * - Real-time status reporting
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// Include all BootBoots libraries
#include "Camera.h"
#include "Atomizer.h"
#include "PCF8574Manager.h"
#include "WifiConnect.h"
#include "HttpClient.h"
#include "SDLogger.h"
#include "MessageQueue.h"
#include "NamedImage.h"
#include "BluetoothService.h"
#include "SystemState.h"
#include "OTAUpdate.h"
#include "BluetoothOTA.h"
#include "secrets.h"
#include "../include/version.h"

// Fallback definitions if not defined in secrets.h
#ifndef API_URL
#define API_URL "https://your-sagemaker-endpoint.amazonaws.com/invocations"
#endif
#ifndef API_KEY
#define API_KEY "your-api-key-here"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "bootboots-ota-2025"
#endif

// System configuration
#define LED_PIN 33              // Built-in red LED for status
#define BUTTON_PIN 0            // Boot button for manual control
#define I2C_SDA 1               // GPIO1 (U0TXD) - SDA
#define I2C_SCL 3               // GPIO3 (U0RXD) - SCL
#define ATOMIZER_CONTROL_PIN 12 // GPIO12 for atomizer control
#define PCF8574_ADDRESS 0x20    // I2C address for PCF8574

// System timing
#define DETECTION_INTERVAL_MS 5000    // 5 seconds between detections
#define STATUS_REPORT_INTERVAL_MS 60000 // 1 minute status reports
#define WATCHDOG_TIMEOUT_MS 30000     // 30 second watchdog timeout

// Cat recognition thresholds (ultra-safe)
#define MIN_DETECTION_CONFIDENCE 0.85f  // Minimum confidence for any detection
#define BOOTS_CONFIDENCE_THRESHOLD 0.90f // 90% minimum for Boots activation (index 0)
#define KAPPA_PROTECTION_THRESHOLD 0.15f // Max 15% Kappa confidence allowed (index 2)

// Global system components
Camera* camera = nullptr;
Atomizer* atomizer = nullptr;
PCF8574Manager* pcfManager = nullptr;
WifiConnect* wifiConnect = nullptr;
CatCam::HttpClient* httpClient = nullptr;
SDLogger* sdLogger = nullptr;
MessageQueue* messageQueue = nullptr;
BootBootsBluetoothService* bluetoothService = nullptr;
OTAUpdate* otaUpdate = nullptr;

// System state instance
SystemState systemState;

// Cat names for logging (index matches AI model output)
const char* CAT_NAMES[6] = {
    "Boots",      // Index 0 - Target for deterrent
    "Chi",        // Index 1
    "Kappa",      // Index 2 - Special protection (misidentified as Boots 67% of time)
    "Mu",         // Index 3
    "Tau",        // Index 4
    "Wolf"        // Index 5
};

// Function declarations
void initializeSystem();
void initializeHardware();
void initializeComponents();
void performSystemChecks();
void mainControlLoop();
void processDetection();
DetectionResult analyzeImage(NamedImage* image);
DetectionResult parseAIResponse(const String& response);
void handleDetectionResult(const DetectionResult& result);
void updateSystemStatus();
void reportSystemStatus();
void handleSystemError(const char* component, const char* error);
void blinkStatusLED(int count, int delayMs = 200);
void setStatusLED(bool on);
bool checkSystemHealth();
void performWatchdogReset();

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    SDLogger::getInstance().infof("\n=== BootBoots System Starting ===");
    
    // Record system start time
    systemState.systemStartTime = millis();
    
    // Disable watchdog timers during initialization
    disableCore0WDT();
    disableCore1WDT();
    
    // Initialize hardware first
    initializeHardware();
    
    // Initialize all system components
    initializeComponents();
    
    // Perform comprehensive system checks
    performSystemChecks();
    
    // Mark system as initialized
    systemState.initialized = true;
    
    SDLogger::getInstance().infof("=== BootBoots System Ready ===");
    SDLogger::getInstance().infof("%s", BANNER);
    
    // Signal successful initialization
    blinkStatusLED(5, 100); // 5 quick blinks
    delay(1000);
    
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
    // Check if system is properly initialized
    if (!systemState.initialized) {
        SDLogger::getInstance().errorf("ERROR: System not initialized, restarting...");
        ESP.restart();
        return;
    }
    
    // Handle OTA updates (highest priority)
    if (otaUpdate) {
        otaUpdate->handle();
        
        // Disable all operations during OTA update for safety
        if (otaUpdate->isUpdating()) {
            systemState.atomizerEnabled = false;
            if (atomizer) {
                atomizer->setEnabled(false);
                atomizer->deactivate(); // Ensure atomizer is off
            }
            delay(100); // Reduce CPU load during update
            return; // Skip all other operations
        }
    }
    
    // Perform system health check
    if (!checkSystemHealth()) {
        handleSystemError("SYSTEM", "Health check failed");
        delay(5000);
        return;
    }
    
    // Main control loop
    mainControlLoop();
    
    // Update system status
    updateSystemStatus();
    
    // Report status periodically
    if (millis() - systemState.lastStatusReport > STATUS_REPORT_INTERVAL_MS) {
        reportSystemStatus();
        systemState.lastStatusReport = millis();
        
        // Update Bluetooth status if connected
        if (bluetoothService && bluetoothService->isConnected()) {
            bluetoothService->updateSystemStatus(systemState);
        }
    }
    
    // Reset watchdog timer
    performWatchdogReset();
    
    // Small delay to prevent overwhelming the system
    delay(100);
}

// ============================================================================
// SYSTEM INITIALIZATION FUNCTIONS
// ============================================================================

void initializeHardware() {
    SDLogger::getInstance().infof("Initializing hardware...");
    
    // Initialize LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // Turn OFF (active LOW)
    
    // Initialize button pin
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 100kHz I2C clock
    systemState.i2cReady = true;
    
    SDLogger::getInstance().infof("Hardware initialization complete");
}

void initializeComponents() {
    SDLogger::getInstance().infof("Initializing system components...");
    
    // Initialize SD Logger first for early logging capability
    if (SDLogger::getInstance().init()) {
        systemState.sdCardReady = true;
        SDLogger::getInstance().infof("SD Logger initialized successfully");
    } else {
        systemState.sdCardReady = false;
        handleSystemError("SD_INIT", "Failed to initialize SD Logger");
    }
    
    // Initialize Camera
    camera = new Camera();
    camera->init();
    systemState.cameraReady = true;
    
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("Camera module initialized");
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
    
    // Initialize HTTP Client
    httpClient = new CatCam::HttpClient();
    CatCam::HttpClient::init();
    SDLogger::getInstance().infof("HTTP Client initialized");
    
    // Initialize PCF8574 Manager
    pcfManager = new PCF8574Manager(PCF8574_ADDRESS);
    if (pcfManager->init(21, 22)) {  // SDA=21, SCL=22 for ESP32
        SDLogger::getInstance().infof("PCF8574 Manager initialized");
        if (systemState.sdCardReady) {
            SDLogger::getInstance().infof("PCF8574 Manager initialized");
        }
    } else {
        SDLogger::getInstance().warnf("WARNING: PCF8574 Manager initialization failed");
    }
    
    // Initialize Atomizer (deterrent system)
    atomizer = new Atomizer(ATOMIZER_CONTROL_PIN);
    atomizer->init();
    atomizer->setEnabled(systemState.atomizerEnabled);
    SDLogger::getInstance().infof("Atomizer initialized");
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("Deterrent system initialized");
    }
    
    // Initialize Message Queue
    messageQueue = new MessageQueue();
    SDLogger::getInstance().infof("Message Queue initialized");
    
    // Initialize Bluetooth Service
    bluetoothService = new BootBootsBluetoothService();
    bluetoothService->init("BootBoots-CatCam");
    SDLogger::getInstance().infof("Bluetooth Service initialized");
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("Bluetooth service initialized");
    }
    
    // Initialize OTA Update Service
    otaUpdate = new OTAUpdate();
    otaUpdate->init("BootBoots-CatCam", OTA_PASSWORD);
    otaUpdate->setUpdateCallback([](bool success, const char* error) {
        if (success) {
            SDLogger::getInstance().infof("OTA Update: %s", error);
        } else {
            SDLogger::getInstance().errorf("OTA Update failed: %s", error);
        }
    });
    SDLogger::getInstance().infof("OTA Update service initialized");
    if (systemState.sdCardReady) {
        SDLogger::getInstance().infof("OTA service initialized - updates available via WiFi");
    }
    
    SDLogger::getInstance().infof("All components initialized");
}

void performSystemChecks() {
    SDLogger::getInstance().infof("Performing system checks...");
    
    // Check critical components
    bool criticalError = false;
    
    if (!systemState.cameraReady) {
        SDLogger::getInstance().errorf("CRITICAL: Camera not ready");
        criticalError = true;
    }
    
    if (!systemState.i2cReady) {
        SDLogger::getInstance().warnf("WARNING: I2C not ready");
    }
    
    if (!systemState.wifiConnected) {
        SDLogger::getInstance().warnf("WARNING: WiFi not connected - operating in offline mode");
    }
    
    if (!systemState.sdCardReady) {
        SDLogger::getInstance().warnf("WARNING: SD Card not ready - logging disabled");
    }
    
    if (criticalError) {
        SDLogger::getInstance().errorf("CRITICAL ERRORS DETECTED - System cannot start");
        while (true) {
            blinkStatusLED(10, 100); // Rapid error blinks
            delay(2000);
        }
    }
    
    SDLogger::getInstance().infof("System checks complete - All critical systems operational");
}

// ============================================================================
// MAIN CONTROL LOOP FUNCTIONS
// ============================================================================

void mainControlLoop() {
    // Check if it's time for a new detection
    if (millis() - systemState.lastDetection < DETECTION_INTERVAL_MS) {
        return; // Not time yet
    }
    
    // Check if atomizer is currently active (don't interfere)
    if (atomizer && atomizer->isActive()) {
        setStatusLED(true); // Keep LED on during atomizer activation
        return;
    }
    
    // Perform detection cycle
    processDetection();
    
    // Update last detection time
    systemState.lastDetection = millis();
}

void processDetection() {
    SDLogger::getInstance().infof("\n--- Starting Detection Cycle ---");
    
    // Capture image from camera
    NamedImage* image = camera->getImage();
    if (!image) {
        handleSystemError("CAMERA", "Failed to capture image");
        return;
    }
    
    SDLogger::getInstance().infof("Image captured successfully");
    
    // Analyze image for cat detection
    DetectionResult result = analyzeImage(image);
    
    // Handle the detection result
    handleDetectionResult(result);
    
    // Clean up image memory
    camera->releaseImageBuffer(image);
    
    SDLogger::getInstance().infof("--- Detection Cycle Complete ---\n");
}

DetectionResult analyzeImage(NamedImage* image) {
    DetectionResult result = {"Unknown", 0.0f, 0, nullptr};
    
    // Check if we have network connectivity for AI inference
    if (!systemState.wifiConnected || !httpClient) {
        SDLogger::getInstance().warnf("WARNING: No network connectivity - skipping AI analysis");
        if (systemState.sdCardReady) {
            SDLogger::getInstance().warnf("Skipped analysis - no network");
        }
        return result;
    }
    
    SDLogger::getInstance().infof("Sending image to AI service...");
    
    // Send image to AI service for analysis
    String response = httpClient->postImage(image, API_URL, API_KEY);
    
    if (response.length() == 0) {
        SDLogger::getInstance().errorf("ERROR: Empty response from AI service");
        if (systemState.sdCardReady) {
            SDLogger::getInstance().errorf("Empty response from AI service");
        }
        return result;
    }
    
    SDLogger::getInstance().infof("AI response received, parsing...");
    
    // Parse AI response
    result = parseAIResponse(response);
    
    return result;
}

DetectionResult parseAIResponse(const String& response) {
    DetectionResult result = {"Unknown", 0.0f, 0, nullptr};
    
    // Parse JSON response from AI service
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        SDLogger::getInstance().errorf("JSON parsing error: %s", error.c_str());
        if (systemState.sdCardReady) {
            SDLogger::getInstance().errorf("JSON parsing error");
        }
        return result;
    }
    
    // Extract detection results
    if (doc.containsKey("predictions") && doc["predictions"].is<JsonArray>()) {
        JsonArray predictions = doc["predictions"];
        
        // Find highest confidence prediction
        float maxConfidence = 0.0f;
        int maxIndex = 0;
        
        for (size_t i = 0; i < predictions.size() && i < 6; i++) {
            float confidence = predictions[i].as<float>();
            if (confidence > maxConfidence) {
                maxConfidence = confidence;
                maxIndex = i;
            }
        }
        
        // Only accept detections above minimum confidence threshold
        if (maxConfidence >= MIN_DETECTION_CONFIDENCE) {
            result.confidence = maxConfidence;
            result.index = maxIndex;
            result.catName = CAT_NAMES[maxIndex];
            
            // Allocate and copy all probabilities for detailed analysis
            result.allProbabilities = new float[6];
            for (size_t i = 0; i < 6; i++) {
                result.allProbabilities[i] = (i < predictions.size()) ? predictions[i].as<float>() : 0.0f;
            }
        }
    }
    
    return result;
}

void handleDetectionResult(const DetectionResult& result) {
    systemState.totalDetections++;
    
    // Log detection result
    SDLogger::getInstance().infof("Detection Result: %s (confidence: %.1f%%)", 
                                 result.catName, result.confidence * 100);
    
    if (systemState.sdCardReady) {
        char logMessage[128];
        snprintf(logMessage, sizeof(logMessage), "Detected: %s (%.1f%% confidence)", 
                result.catName, result.confidence * 100);
        SDLogger::getInstance().infof("%s", logMessage);
    }
    
    // Check if this is a valid detection
    if (result.confidence < MIN_DETECTION_CONFIDENCE) {
        SDLogger::getInstance().infof("Detection below confidence threshold - ignoring");
        return;
    }
    
    // Special handling for Boots detection (target for deterrent)
    if (result.index == 0) { // Boots is index 0
        systemState.bootsDetections++;
        SDLogger::getInstance().criticalf("BOOTS DETECTED - Evaluating deterrent activation");
        
        // Check if atomizer should activate
        if (atomizer && atomizer->shouldActivate(result)) {
            SDLogger::getInstance().criticalf("ACTIVATING DETERRENT SYSTEM");
            atomizer->activate();
            systemState.atomizerActivations++;
            
            if (systemState.sdCardReady) {
                atomizer->logActivation(result);
            }
            
            // Visual feedback
            blinkStatusLED(3, 500); // 3 long blinks for activation
        } else {
            SDLogger::getInstance().warnf("Deterrent activation rejected - safety thresholds not met");
            systemState.falsePositivesAvoided++;
            
            if (systemState.sdCardReady) {
                atomizer->logRejection(result, "Safety thresholds not met");
            }
        }
    } else {
        // Detection of other cats
        SDLogger::getInstance().infof("Detected friendly cat: %s", result.catName);
        
        // Special protection for Kappa (index 2) - misidentified as Boots 67% of time
        if (result.index == 2) {
            SDLogger::getInstance().infof("KAPPA DETECTED - Protected cat, no deterrent");
        }
    }
    
    // Clean up allocated memory
    if (result.allProbabilities) {
        delete[] result.allProbabilities;
    }
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
    blinkStatusLED(5, 200); // 5 fast blinks for error
}

// ============================================================================
// SYSTEM STATUS AND MONITORING FUNCTIONS
// ============================================================================

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
    
    // Update status LED
    if (systemState.wifiConnected) {
        setStatusLED(false); // LED off when everything is good
    } else {
        // Slow blink when offline
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 2000) {
            static bool ledState = false;
            setStatusLED(ledState);
            ledState = !ledState;
            lastBlink = millis();
        }
    }
}

void reportSystemStatus() {
    unsigned long uptime = millis() - systemState.systemStartTime;
    
    SDLogger::getInstance().infof("\n=== SYSTEM STATUS REPORT ===");
    SDLogger::getInstance().infof("Uptime: %lu seconds", uptime / 1000);
    SDLogger::getInstance().infof("Total Detections: %d", systemState.totalDetections);
    SDLogger::getInstance().infof("Boots Detections: %d", systemState.bootsDetections);
    SDLogger::getInstance().infof("Atomizer Activations: %d", systemState.atomizerActivations);
    SDLogger::getInstance().infof("False Positives Avoided: %d", systemState.falsePositivesAvoided);
    SDLogger::getInstance().infof("Camera Ready: %s", systemState.cameraReady ? "YES" : "NO");
    SDLogger::getInstance().infof("WiFi Connected: %s", systemState.wifiConnected ? "YES" : "NO");
    SDLogger::getInstance().infof("SD Card Ready: %s", systemState.sdCardReady ? "YES" : "NO");
    SDLogger::getInstance().infof("Atomizer Enabled: %s", systemState.atomizerEnabled ? "YES" : "NO");
    
    if (atomizer) {
        SDLogger::getInstance().infof("Atomizer Can Activate: %s", atomizer->canActivate() ? "YES" : "NO");
    }
    
    SDLogger::getInstance().infof("==============================\n");
    
    // Log status to SD card
    if (systemState.sdCardReady) {
        char statusMessage[256];
        snprintf(statusMessage, sizeof(statusMessage), 
                "Uptime: %lus, Detections: %d, Boots: %d, Activations: %d, False+ Avoided: %d",
                uptime / 1000, systemState.totalDetections, systemState.bootsDetections,
                systemState.atomizerActivations, systemState.falsePositivesAvoided);
        SDLogger::getInstance().infof("%s", statusMessage);
    }
}

bool checkSystemHealth() {
    // Check critical system components
    if (!systemState.cameraReady) {
        return false;
    }
    
    // Check memory usage
    if (ESP.getFreeHeap() < 10000) { // Less than 10KB free
        SDLogger::getInstance().warnf("WARNING: Low memory detected");
        return false;
    }
    
    // Check if atomizer is stuck active
    if (atomizer && atomizer->isActive()) {
        static unsigned long atomizer_check = 0;
        if (atomizer_check == 0) {
            atomizer_check = millis();
        } else if (millis() - atomizer_check > 10000) { // Active for more than 10 seconds
            SDLogger::getInstance().warnf("WARNING: Atomizer stuck active - forcing deactivation");
            atomizer->deactivate();
            atomizer_check = 0;
        }
    } else {
        // Reset check timer when atomizer is not active
        static unsigned long atomizer_check = 0;
        atomizer_check = 0;
    }
    
    return true;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void blinkStatusLED(int count, int delayMs) {
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_PIN, LOW);  // Turn ON (active LOW)
        delay(delayMs);
        digitalWrite(LED_PIN, HIGH); // Turn OFF (active LOW)
        delay(delayMs);
    }
}

void setStatusLED(bool on) {
    digitalWrite(LED_PIN, on ? LOW : HIGH); // Active LOW
}

void performWatchdogReset() {
    // Reset watchdog timer to prevent system reset
    yield();
    
    // Re-enable watchdog timers with longer timeout
    static unsigned long lastWatchdogReset = 0;
    if (millis() - lastWatchdogReset > WATCHDOG_TIMEOUT_MS) {
        esp_task_wdt_reset();
        lastWatchdogReset = millis();
    }
}