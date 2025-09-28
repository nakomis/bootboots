#include "OTAUpdate.h"
#include <HTTPClient.h>

// Static instance for callbacks
OTAUpdate* OTAUpdate::_instance = nullptr;

OTAUpdate::OTAUpdate() {
    _initialized = false;
    _updating = false;
    _secureMode = true;
    _progress = 0;
    _status = "Not initialized";
    _updateCallback = nullptr;
    _httpClient = new HTTPClient();
    _httpUpdateInProgress = false;
    _totalSize = 0;
    _downloadedSize = 0;
    _instance = this;
}

OTAUpdate::~OTAUpdate() {
    if (_httpClient) {
        delete _httpClient;
        _httpClient = nullptr;
    }
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
    return _updating || _httpUpdateInProgress;
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

// HTTP OTA Update Implementation
bool OTAUpdate::updateFromURL(const char* firmwareURL) {
    if (!_initialized) {
        SDLogger::getInstance().errorf("OTA not initialized");
        return false;
    }
    
    if (_updating || _httpUpdateInProgress) {
        SDLogger::getInstance().warnf("Update already in progress");
        return false;
    }
    
    SDLogger::getInstance().infof("Starting HTTP OTA update from: %s", firmwareURL);
    
    _httpUpdateInProgress = true;
    _updating = true;
    _progress = 0;
    _status = "Connecting to firmware server...";
    
    // Configure HTTP client
    _httpClient->begin(firmwareURL);
    _httpClient->setTimeout(30000); // 30 second timeout
    _httpClient->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    // Get firmware size
    int httpCode = _httpClient->GET();
    
    if (httpCode != HTTP_CODE_OK) {
        SDLogger::getInstance().errorf("HTTP GET failed: %d", httpCode);
        _status = "Failed to connect to firmware server";
        _httpUpdateInProgress = false;
        _updating = false;
        if (_updateCallback) {
            _updateCallback(false, "Failed to connect to firmware server");
        }
        _httpClient->end();
        return false;
    }
    
    _totalSize = _httpClient->getSize();
    _downloadedSize = 0;
    
    if (_totalSize <= 0) {
        SDLogger::getInstance().errorf("Invalid firmware size: %d", _totalSize);
        _status = "Invalid firmware file";
        _httpUpdateInProgress = false;
        _updating = false;
        if (_updateCallback) {
            _updateCallback(false, "Invalid firmware file");
        }
        _httpClient->end();
        return false;
    }
    
    SDLogger::getInstance().infof("Firmware size: %d bytes", _totalSize);
    _status = "Starting firmware download...";
    
    // Begin update process
    if (!Update.begin(_totalSize)) {
        SDLogger::getInstance().errorf("Update.begin failed: %s", Update.errorString());
        _status = "Failed to begin update process";
        _httpUpdateInProgress = false;
        _updating = false;
        if (_updateCallback) {
            _updateCallback(false, Update.errorString());
        }
        _httpClient->end();
        return false;
    }
    
    // Download and write firmware
    WiFiClient* stream = _httpClient->getStreamPtr();
    uint8_t buffer[1024];
    size_t bytesRead = 0;
    
    while (_httpClient->connected() && _downloadedSize < _totalSize) {
        size_t available = stream->available();
        if (available > 0) {
            size_t readBytes = stream->readBytes(buffer, min(available, sizeof(buffer)));
            
            if (Update.write(buffer, readBytes) != readBytes) {
                SDLogger::getInstance().errorf("Update.write failed: %s", Update.errorString());
                _status = "Failed to write firmware data";
                Update.abort();
                _httpUpdateInProgress = false;
                _updating = false;
                if (_updateCallback) {
                    _updateCallback(false, "Failed to write firmware data");
                }
                _httpClient->end();
                return false;
            }
            
            _downloadedSize += readBytes;
            _progress = (_downloadedSize * 100) / _totalSize;
            
            // Log progress every 10%
            static int lastProgressLogged = -1;
            int currentProgress = _progress / 10;
            if (currentProgress > lastProgressLogged) {
                SDLogger::getInstance().infof("Download progress: %d%% (%d/%d bytes)", 
                                            _progress, _downloadedSize, _totalSize);
                _status = "Downloading: " + String(_progress) + "%";
                lastProgressLogged = currentProgress;
            }
        } else {
            delay(10);
        }
    }
    
    _httpClient->end();
    
    // Finalize update
    if (_downloadedSize == _totalSize && Update.end(true)) {
        SDLogger::getInstance().infof("HTTP OTA update completed successfully");
        _status = "Update completed - rebooting...";
        _progress = 100;
        
        if (_updateCallback) {
            _updateCallback(true, "Update completed successfully");
        }
        
        delay(2000);
        ESP.restart();
        return true;
    } else {
        SDLogger::getInstance().errorf("HTTP OTA update failed: %s", Update.errorString());
        _status = "Update failed: " + String(Update.errorString());
        Update.abort();
        _httpUpdateInProgress = false;
        _updating = false;
        
        if (_updateCallback) {
            _updateCallback(false, Update.errorString());
        }
        return false;
    }
}

void OTAUpdate::cancelUpdate() {
    if (_httpUpdateInProgress) {
        SDLogger::getInstance().warnf("Cancelling HTTP OTA update");
        _httpClient->end();
        Update.abort();
        _httpUpdateInProgress = false;
        _updating = false;
        _status = "Update cancelled";
        _progress = 0;
        
        if (_updateCallback) {
            _updateCallback(false, "Update cancelled by user");
        }
    }
}
