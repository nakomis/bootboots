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

// Include all BootBoots libraries
#include "Camera.h"
#include "Atomizer.h"
#include "PCF8574Manager.h"
#include "WifiConnect.h"
#include "HttpClient.h"
#include "SDLogger.h"
#include "MessageQueue.h"
#include "NamedImage.h"
#include "secrets.h"

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

// System state
struct SystemState {
    bool initialized = false;
    bool cameraReady = false;
    bool wifiConnected = false;
    bool sdCardReady = false;
    bool i2cReady = false;
    bool atomizerEnabled = true;
    unsigned long lastDetection = 0;
    unsigned long lastStatusReport = 0;
    unsigned long systemStartTime = 0;
    int totalDetections = 0;
    int bootsDetections = 0;
    int falsePositivesAvoided = 0;
    int atomizerActivations = 0;
} systemState;

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
    Serial.println("\n=== BootBoots System Starting ===");
    
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
    
    Serial.println("=== BootBoots System Ready ===");
    Serial.println(BANNER);
    
    // Signal successful initialization
    blinkStatusLED(5, 100); // 5 quick blinks
    delay(1000);
    
    // Log system startup
    if (sdLogger && systemState.sdCardReady) {
        sdLogger->logEvent("SYSTEM", "BootBoots system initialized successfully");
        sdLogger->logSystemStatus(systemState.cameraReady, systemState.wifiConnected, 
                                systemState.i2cReady, systemState.atomizerEnabled);
    }
}

void loop() {
    // Check if system is properly initialized
    if (!systemState.initialized) {
        Serial.println("ERROR: System not initialized, restarting...");
        ESP.restart();
        return;
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
    Serial.println("Initializing hardware...");
    
    // Initialize LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // Turn OFF (active LOW)
    
    // Initialize button pin
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 100kHz I2C clock
    systemState.i2cReady = true;
    
    Serial.println("Hardware initialization complete");
}

void initializeComponents() {
    Serial.println("Initializing system components...");
    
    // Initialize SD Logger first for early logging capability
    sdLogger = new SDLogger();
    if (sdLogger->init()) {
        systemState.sdCardReady = true;
        Serial.println("SD Logger initialized");
        sdLogger->logEvent("SYSTEM", "SD Logger initialized successfully");
    } else {
        Serial.println("WARNING: SD Logger initialization failed");
        systemState.sdCardReady = false;
    }
    
    // Initialize Camera
    camera = new Camera();
    camera->init();
    systemState.cameraReady = true;
    Serial.println("Camera initialized");
    if (sdLogger && systemState.sdCardReady) {
        sdLogger->logEvent("CAMERA", "Camera module initialized");
    }
    
    // Initialize WiFi
    wifiConnect = new WifiConnect();
    if (wifiConnect->connect(WIFI_SSID, WIFI_PASSWORD)) {
        systemState.wifiConnected = true;
        Serial.println("WiFi connected successfully");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("WIFI", "WiFi connected successfully");
        }
    } else {
        Serial.println("WARNING: WiFi connection failed");
        systemState.wifiConnected = false;
    }
    
    // Initialize HTTP Client
    httpClient = new CatCam::HttpClient();
    CatCam::HttpClient::init();
    Serial.println("HTTP Client initialized");
    
    // Initialize PCF8574 Manager
    pcfManager = new PCF8574Manager(PCF8574_ADDRESS);
    if (pcfManager->init()) {
        Serial.println("PCF8574 Manager initialized");
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("I2C", "PCF8574 Manager initialized");
        }
    } else {
        Serial.println("WARNING: PCF8574 Manager initialization failed");
    }
    
    // Initialize Atomizer (deterrent system)
    atomizer = new Atomizer(ATOMIZER_CONTROL_PIN);
    atomizer->init();
    atomizer->setEnabled(systemState.atomizerEnabled);
    Serial.println("Atomizer initialized");
    if (sdLogger && systemState.sdCardReady) {
        sdLogger->logEvent("ATOMIZER", "Deterrent system initialized");
    }
    
    // Initialize Message Queue
    messageQueue = new MessageQueue();
    Serial.println("Message Queue initialized");
    
    Serial.println("All components initialized");
}

void performSystemChecks() {
    Serial.println("Performing system checks...");
    
    // Check critical components
    bool criticalError = false;
    
    if (!systemState.cameraReady) {
        Serial.println("CRITICAL: Camera not ready");
        criticalError = true;
    }
    
    if (!systemState.i2cReady) {
        Serial.println("WARNING: I2C not ready");
    }
    
    if (!systemState.wifiConnected) {
        Serial.println("WARNING: WiFi not connected - operating in offline mode");
    }
    
    if (!systemState.sdCardReady) {
        Serial.println("WARNING: SD Card not ready - logging disabled");
    }
    
    if (criticalError) {
        Serial.println("CRITICAL ERRORS DETECTED - System cannot start");
        while (true) {
            blinkStatusLED(10, 100); // Rapid error blinks
            delay(2000);
        }
    }
    
    Serial.println("System checks complete - All critical systems operational");
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
    Serial.println("\n--- Starting Detection Cycle ---");
    
    // Capture image from camera
    NamedImage* image = camera->getImage();
    if (!image) {
        handleSystemError("CAMERA", "Failed to capture image");
        return;
    }
    
    Serial.println("Image captured successfully");
    
    // Analyze image for cat detection
    DetectionResult result = analyzeImage(image);
    
    // Handle the detection result
    handleDetectionResult(result);
    
    // Clean up image memory
    camera->releaseImageBuffer(image);
    
    Serial.println("--- Detection Cycle Complete ---\n");
}

DetectionResult analyzeImage(NamedImage* image) {
    DetectionResult result = {"Unknown", 0.0f, 0, nullptr};
    
    // Check if we have network connectivity for AI inference
    if (!systemState.wifiConnected || !httpClient) {
        Serial.println("WARNING: No network connectivity - skipping AI analysis");
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("AI", "Skipped analysis - no network");
        }
        return result;
    }
    
    Serial.println("Sending image to AI service...");
    
    // Send image to AI service for analysis
    String response = httpClient->postImage(image, API_URL, API_KEY);
    
    if (response.length() == 0) {
        Serial.println("ERROR: Empty response from AI service");
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("AI", "Empty response from AI service");
        }
        return result;
    }
    
    Serial.println("AI response received, parsing...");
    
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
        Serial.print("JSON parsing error: ");
        Serial.println(error.c_str());
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("AI", "JSON parsing error");
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
    Serial.print("Detection Result: ");
    Serial.print(result.catName);
    Serial.print(" (confidence: ");
    Serial.print(result.confidence * 100, 1);
    Serial.println("%)");
    
    if (sdLogger && systemState.sdCardReady) {
        char logMessage[128];
        snprintf(logMessage, sizeof(logMessage), "Detected: %s (%.1f%% confidence)", 
                result.catName, result.confidence * 100);
        sdLogger->logEvent("DETECTION", logMessage);
    }
    
    // Check if this is a valid detection
    if (result.confidence < MIN_DETECTION_CONFIDENCE) {
        Serial.println("Detection below confidence threshold - ignoring");
        return;
    }
    
    // Special handling for Boots detection (target for deterrent)
    if (result.index == 0) { // Boots is index 0
        systemState.bootsDetections++;
        Serial.println("BOOTS DETECTED - Evaluating deterrent activation");
        
        // Check if atomizer should activate
        if (atomizer && atomizer->shouldActivate(result)) {
            Serial.println("ACTIVATING DETERRENT SYSTEM");
            atomizer->activate();
            systemState.atomizerActivations++;
            
            if (sdLogger && systemState.sdCardReady) {
                atomizer->logActivation(result);
            }
            
            // Visual feedback
            blinkStatusLED(3, 500); // 3 long blinks for activation
        } else {
            Serial.println("Deterrent activation rejected - safety thresholds not met");
            systemState.falsePositivesAvoided++;
            
            if (sdLogger && systemState.sdCardReady) {
                atomizer->logRejection(result, "Safety thresholds not met");
            }
        }
    } else {
        // Detection of other cats
        Serial.print("Detected friendly cat: ");
        Serial.println(result.catName);
        
        // Special protection for Kappa (index 2) - misidentified as Boots 67% of time
        if (result.index == 2) {
            Serial.println("KAPPA DETECTED - Protected cat, no deterrent");
        }
    }
    
    // Clean up allocated memory
    if (result.allProbabilities) {
        delete[] result.allProbabilities;
    }
}

// ============================================================================
// SYSTEM STATUS AND MONITORING FUNCTIONS
// ============================================================================

void updateSystemStatus() {
    // Update WiFi connection status
    if (systemState.wifiConnected && WiFi.status() != WL_CONNECTED) {
        systemState.wifiConnected = false;
        Serial.println("WARNING: WiFi connection lost");
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("WIFI", "Connection lost");
        }
    } else if (!systemState.wifiConnected && WiFi.status() == WL_CONNECTED) {
        systemState.wifiConnected = true;
        Serial.println("WiFi connection restored");
        if (sdLogger && systemState.sdCardReady) {
            sdLogger->logEvent("WIFI", "Connection restored");
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
    
    Serial.println("\n=== SYSTEM STATUS REPORT ===");
    Serial.print("Uptime: ");
    Serial.print(uptime / 1000);
    Serial.println(" seconds");
    
    Serial.print("Total Detections: ");
    Serial.println(systemState.totalDetections);
    
    Serial.print("Boots Detections: ");
    Serial.println(systemState.bootsDetections);
    
    Serial.print("Atomizer Activations: ");
    Serial.println(systemState.atomizerActivations);
    
    Serial.print("False Positives Avoided: ");
    Serial.println(systemState.falsePositivesAvoided);
    
    Serial.print("Camera Ready: ");
    Serial.println(systemState.cameraReady ? "YES" : "NO");
    
    Serial.print("WiFi Connected: ");
    Serial.println(systemState.wifiConnected ? "YES" : "NO");
    
    Serial.print("SD Card Ready: ");
    Serial.println(systemState.sdCardReady ? "YES" : "NO");
    
    Serial.print("Atomizer Enabled: ");
    Serial.println(systemState.atomizerEnabled ? "YES" : "NO");
    
    if (atomizer) {
        Serial.print("Atomizer Can Activate: ");
        Serial.println(atomizer->canActivate() ? "YES" : "NO");
    }
    
    Serial.println("==============================\n");
    
    // Log status to SD card
    if (sdLogger && systemState.sdCardReady) {
        char statusMessage[256];
        snprintf(statusMessage, sizeof(statusMessage), 
                "Uptime: %lus, Detections: %d, Boots: %d, Activations: %d, False+ Avoided: %d",
                uptime / 1000, systemState.totalDetections, systemState.bootsDetections,
                systemState.atomizerActivations, systemState.falsePositivesAvoided);
        sdLogger->logEvent("STATUS", statusMessage);
    }
}

bool checkSystemHealth() {
    // Check critical system components
    if (!systemState.cameraReady) {
        return false;
    }
    
    // Check memory usage
    if (ESP.getFreeHeap() < 10000) { // Less than 10KB free
        Serial.println("WARNING: Low memory detected");
        return false;
    }
    
    // Check if atomizer is stuck active
    if (atomizer && atomizer->isActive()) {
        static unsigned long atomizer_check = 0;
        if (atomizer_check == 0) {
            atomizer_check = millis();
        } else if (millis() - atomizer_check > 10000) { // Active for more than 10 seconds
            Serial.println("WARNING: Atomizer stuck active - forcing deactivation");
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

void handleSystemError(const char* component, const char* error) {
    Serial.print("SYSTEM ERROR in ");
    Serial.print(component);
    Serial.print(": ");
    Serial.println(error);
    
    // Log error to SD card if available
    if (sdLogger && systemState.sdCardReady) {
        char errorMessage[128];
        snprintf(errorMessage, sizeof(errorMessage), "ERROR in %s: %s", component, error);
        sdLogger->logEvent("ERROR", errorMessage);
    }
    
    // Visual error indication
    blinkStatusLED(10, 100); // Rapid error blinks
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