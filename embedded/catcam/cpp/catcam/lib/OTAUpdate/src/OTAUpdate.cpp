#include "OTAUpdate.h"

// Static instance for callbacks
OTAUpdate* OTAUpdate::_instance = nullptr;

OTAUpdate::OTAUpdate() {
    _initialized = false;
    _updating = false;
    _secureMode = true;
    _progress = 0;
    _status = "Not initialized";
    _updateCallback = nullptr;
    _instance = this;
}

void OTAUpdate::init(const char* hostname, const char* password) {
    if (_initialized) {
        SDLogger::getInstance().warnf("OTA already initialized");
        return;
    }
    
    _hostname = String(hostname);
    if (password) {
        _password = String(password);
    }
    
    SDLogger::getInstance().infof("Initializing OTA updates...");
    
    // Set hostname for mDNS
    ArduinoOTA.setHostname(hostname);
    
    // Set password if provided
    if (password && strlen(password) > 0) {
        ArduinoOTA.setPassword(password);
        SDLogger::getInstance().infof("OTA password protection enabled");
    } else if (_secureMode) {
        SDLogger::getInstance().warnf("WARNING: OTA running without password in secure mode");
    }
    
    // Configure OTA callbacks
    ArduinoOTA.onStart([]() {
        if (_instance) _instance->onStart();
    });
    
    ArduinoOTA.onEnd([]() {
        if (_instance) _instance->onEnd();
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (_instance) _instance->onProgress(progress, total);
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        if (_instance) _instance->onError(error);
    });
    
    // Start OTA service
    ArduinoOTA.begin();
    
    // Initialize mDNS for discovery
    if (!MDNS.begin(hostname)) {
        SDLogger::getInstance().errorf("Error setting up mDNS responder");
    } else {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ota", "tcp", 3232);
        SDLogger::getInstance().infof("mDNS responder started: %s.local", hostname);
    }
    
    _initialized = true;
    _status = "Ready for updates";
    SDLogger::getInstance().infof("OTA update service initialized successfully");
}

void OTAUpdate::handle() {
    if (!_initialized) {
        return;
    }
    
    ArduinoOTA.handle();
    // MDNS.update() not available in this ESP32 Arduino framework version
}

bool OTAUpdate::isUpdating() {
    return _updating;
}

void OTAUpdate::setUpdateCallback(void (*callback)(bool success, const char* error)) {
    _updateCallback = callback;
}

void OTAUpdate::setPassword(const char* password) {
    if (password) {
        _password = String(password);
        ArduinoOTA.setPassword(password);
        SDLogger::getInstance().infof("OTA password updated");
    }
}

void OTAUpdate::enableSecureMode(bool enable) {
    _secureMode = enable;
    if (enable && _password.length() == 0) {
        SDLogger::getInstance().warnf("WARNING: Secure mode enabled but no password set");
    }
}

String OTAUpdate::getStatus() {
    return _status;
}

int OTAUpdate::getProgress() {
    return _progress;
}

void OTAUpdate::onStart() {
    if (!_instance) return;
    
    _instance->_updating = true;
    _instance->_progress = 0;
    
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    _instance->_status = "Starting " + type + " update";
    
    SDLogger::getInstance().criticalf("*** OTA UPDATE STARTED ***");
    SDLogger::getInstance().criticalf("Update type: %s", type.c_str());
    SDLogger::getInstance().criticalf("Free heap: %d bytes", ESP.getFreeHeap());
    
    // Disable deterrent system during update for safety
    _instance->_status = "OTA update in progress - system disabled";
    
    if (_instance->_updateCallback) {
        _instance->_updateCallback(true, "Update started");
    }
}

void OTAUpdate::onEnd() {
    if (!_instance) return;
    
    _instance->_updating = false;
    _instance->_progress = 100;
    _instance->_status = "Update completed - rebooting";
    
    SDLogger::getInstance().criticalf("*** OTA UPDATE COMPLETED ***");
    SDLogger::getInstance().criticalf("Rebooting in 2 seconds...");
    
    if (_instance->_updateCallback) {
        _instance->_updateCallback(true, "Update completed successfully");
    }
    
    delay(2000);
}

void OTAUpdate::onProgress(unsigned int progress, unsigned int total) {
    if (!_instance) return;
    
    _instance->_progress = (progress / (total / 100));
    
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) { // Log every 5 seconds
        SDLogger::getInstance().infof("OTA Progress: %d%% (%u/%u bytes)", _instance->_progress, progress, total);
        _instance->_status = "Updating: " + String(_instance->_progress) + "%";
        lastLog = millis();
    }
}

void OTAUpdate::onError(ota_error_t error) {
    if (!_instance) return;
    
    _instance->_updating = false;
    _instance->_progress = 0;
    
    String errorMsg;
    switch (error) {
        case OTA_AUTH_ERROR:
            errorMsg = "Auth Failed";
            break;
        case OTA_BEGIN_ERROR:
            errorMsg = "Begin Failed";
            break;
        case OTA_CONNECT_ERROR:
            errorMsg = "Connect Failed";
            break;
        case OTA_RECEIVE_ERROR:
            errorMsg = "Receive Failed";
            break;
        case OTA_END_ERROR:
            errorMsg = "End Failed";
            break;
        default:
            errorMsg = "Unknown Error";
            break;
    }
    
    _instance->_status = "Update failed: " + errorMsg;
    
    SDLogger::getInstance().errorf("*** OTA UPDATE FAILED ***");
    SDLogger::getInstance().errorf("Error: %s", errorMsg.c_str());
    
    if (_instance->_updateCallback) {
        _instance->_updateCallback(false, errorMsg.c_str());
    }
}
