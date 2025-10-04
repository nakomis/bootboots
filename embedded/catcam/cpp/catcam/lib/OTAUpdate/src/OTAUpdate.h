#ifndef CATCAM_OTAUPDATE_H
#define CATCAM_OTAUPDATE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include "../../SDLogger/src/SDLogger.h"

// Forward declaration
class HTTPClient;

class OTAUpdate {
public:
    OTAUpdate();
    ~OTAUpdate();
    void init(const char* hostname = "BootBoots-CatCam", const char* password = nullptr);
    void handle();
    bool isUpdating();
    void setUpdateCallback(void (*callback)(bool success, const char* error));
    
    // HTTP OTA update from URL (e.g., S3 signed URL)
    bool updateFromURL(const char* firmwareURL);
    void cancelUpdate();

    // Two-stage OTA: Download to SD card, then flash on next boot
    bool downloadToSD(const char* firmwareURL);
    static bool flashFromSD();
    static bool hasPendingUpdate();

    // Security features
    void setPassword(const char* password);
    void enableSecureMode(bool enable = true);

    // Status reporting
    String getStatus();
    int getProgress();
    
private:
    bool _initialized;
    bool _updating;
    bool _secureMode;
    String _hostname;
    String _password;
    int _progress;
    String _status;
    void (*_updateCallback)(bool success, const char* error);
    
    // HTTP OTA members
    HTTPClient* _httpClient;
    WiFiClient* _client;           // For HTTP connections
    WiFiClientSecure* _secureClient; // For HTTPS connections
    bool _httpUpdateInProgress;
    size_t _totalSize;
    size_t _downloadedSize;
    
    // OTA event handlers
    static void onStart();
    static void onEnd();
    static void onProgress(unsigned int progress, unsigned int total);
    static void onError(ota_error_t error);
    
    // Static instance for callbacks
    static OTAUpdate* _instance;
};

#endif
