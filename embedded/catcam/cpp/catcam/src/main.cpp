#include <Arduino.h>
#include <Wire.h>
#include <esp_camera.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "main.h"
#include "secrets.h"
#include "WifiConnect.h"
#include "MessageQueue.h"
#include "Camera.h"
#include "../lib/HttpClient/src/HttpClient.h"
#include "../lib/Atomizer/src/Atomizer.h"
#include "../lib/SDLogger/src/SDLogger.h"

WifiConnect wifiConnect;
MessageQueue messageQueue;
Camera camera;
CatCam::HttpClient httpClient;
Preferences preferences;
Atomizer atomizer(GPIO_NUM_2); // Using GPIO 2 for atomizer control

// Function declarations
void processDetectionResponse(const String& jsonResponse);
void deepsleep();

void setup() {
    Serial.begin(115200);
    Serial.println(BANNER); // Keep banner on Serial for immediate startup visibility

    // Initialize HTTP client (must be done before any HTTP requests)
    CatCam::HttpClient::init();

    // Initialize SD card logger for persistent logging
    if (!SDLogger::getInstance().init(5, "/logs")) {
        Serial.println("Warning: SD logger initialization failed - continuing without SD logging");
    } else {
        LOG_I("SD card logger initialized successfully");
    }
    
    // Initialize atomizer deterrent system (SAFETY MODE: Kappa protection priority)
    atomizer.init();

    pinMode(4, INPUT);
    digitalWrite(4, LOW);
    rtc_gpio_hold_dis(GPIO_NUM_4);

    pinMode(GPIO_NUM_15, OUTPUT);
    digitalWrite(GPIO_NUM_15, HIGH);

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

    // Wait three seconds for the kitty to get in range
    LOG_I("Waiting for 3 seconds before taking picture...");
    delay(500);

    // Initialize Camera
    camera.init();

    // Take Picture with Camera
    digitalWrite(GPIO_NUM_15, LOW);
    delay(10);
    NamedImage *namedImage = camera.getImage();
    if (!namedImage)
    {
        digitalWrite(GPIO_NUM_15, LOW);
        ESP.restart(); // Restart if no image is captured
    }
    digitalWrite(GPIO_NUM_15, HIGH);

    LOG_I("Picture taken successfully, now saving to preferences...");

    // Save picture number to preferences
    preferences.begin("catcam", false);
    int pictureNumber = preferences.getInt("pictureNumber", 0);
    LOG_IF("Current picture number: %d", pictureNumber);
    pictureNumber++;
    preferences.putInt("pictureNumber", pictureNumber);
    LOG_IF("Incremented picture number to: %d", pictureNumber);
    preferences.end();

    // Copy image to PSRAM before network operations
    camera.copyImageToPSRAM(namedImage);

    // Post Image to REST API
    LOG_I("Posting image to REST API...");
    wifiConnect.connect();
    
    // Post image and get JSON response
    String jsonResponse = httpClient.postImage(namedImage, API_URL, API_KEY);
    
    // Release the image buffer after posting
    camera.releaseImageBuffer(namedImage);

    // Output JSON response via Serial and SD card
    LOG_I("JSON Response from API:");
    LOG_I(jsonResponse.c_str());
    
    // Parse API response and evaluate deterrent activation (KAPPA PROTECTION PRIORITY)
    processDetectionResponse(jsonResponse);
    
    LOG_I("Image posted successfully");
    camera.deInit();
    LOG_I("Camera resources returned successfully");

    deepsleep();
}

void processDetectionResponse(const String& jsonResponse) {
    LOG_I("=== DETERRENT EVALUATION (KAPPA PROTECTION PRIORITY) ===");
    
    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (error) {
        LOG_EF("JSON parsing failed: %s", error.c_str());
        return;
    }
    
    // Check if response has the expected structure
    if (!doc["success"] || !doc["mostLikelyCat"] || !doc["data"]["probabilities"]) {
        LOG_E("Invalid API response structure - deterrent evaluation skipped");
        return;
    }
    
    // Extract mostLikelyCat data
    const char* catName = doc["mostLikelyCat"]["name"];
    float confidence = doc["mostLikelyCat"]["confidence"];
    int index = doc["mostLikelyCat"]["index"];
    
    // Extract all probabilities for validation
    JsonArray probArray = doc["data"]["probabilities"];
    if (probArray.size() != 6) {
        LOG_E("Invalid probabilities array - deterrent evaluation skipped");
        return;
    }
    
    // Convert to float array for DetectionResult
    static float allProbabilities[6];
    for (int i = 0; i < 6; i++) {
        allProbabilities[i] = probArray[i];
    }
    
    // Create detection result structure
    DetectionResult result = {
        .catName = catName,
        .confidence = confidence,
        .index = index,
        .allProbabilities = allProbabilities
    };
    
    // Log detection details
    LOG_IF("Detection: %s (%.1f%% confidence, index %d)", catName, confidence * 100, index);
    LOG_IF("All probabilities: [%.1f%%, %.1f%%, %.1f%%, %.1f%%, %.1f%%, %.1f%%]",
           allProbabilities[0] * 100, allProbabilities[1] * 100,
           allProbabilities[2] * 100, allProbabilities[3] * 100,
           allProbabilities[4] * 100, allProbabilities[5] * 100);
    
    // Get picture number for logging
    preferences.begin("catcam", true);
    int pictureNumber = preferences.getInt("pictureNumber", 0);
    preferences.end();
    
    // Log to SD card
    SDLogger::getInstance().logDetection(catName, confidence, pictureNumber);
    SDLogger::getInstance().infof("All probabilities: [%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]",
                                  allProbabilities[0] * 100, allProbabilities[1] * 100,
                                  allProbabilities[2] * 100, allProbabilities[3] * 100,
                                  allProbabilities[4] * 100, allProbabilities[5] * 100);
    
    // CRITICAL: Special protection for Kappa (index 2)
    if (index == 2) {
        LOG_C("*** KAPPA DETECTED - DETERRENT PERMANENTLY BLOCKED ***");
        LOG_C("Protecting timid cat from any accidental activation");
        LOG_CF("KAPPA PROTECTION: Deterrent permanently blocked for timid cat (%.1f%% confidence)", confidence * 100);
        return;
    }
    
    // Evaluate deterrent activation with ultra-safe validation
    if (atomizer.shouldActivate(result)) {
        LOG_C("*** DETERRENT CRITERIA MET - ACTIVATING ATOMIZER ***");
        atomizer.logActivation(result);
        atomizer.activate(2000); // 2 second activation
        
        // Wait for activation to complete
        while (atomizer.isActive()) {
            delay(100);
        }
        LOG_C("*** DETERRENT CYCLE COMPLETE ***");
    } else {
        LOG_I("Deterrent activation criteria not met - system safe");
    }
    
    LOG_I("=== DETERRENT EVALUATION COMPLETE ===");
}

void loop() {
  // Should never reach here, but if it does, go to deep sleep
  LOG_W("Loop reached, going to deep sleep...");
  deepsleep();
}

void deepsleep() {
    // Prepare for deep sleep
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    rtc_gpio_hold_en(GPIO_NUM_4);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);
    rtc_gpio_pullup_dis(GPIO_NUM_13);
    rtc_gpio_pulldown_en(GPIO_NUM_13);

    LOG_I("Taking a 3 minute nap before deep sleep...");
    delay(3000);
    LOG_I("Going to deep sleep now");
    esp_deep_sleep_start();
    // This will never be reached
}