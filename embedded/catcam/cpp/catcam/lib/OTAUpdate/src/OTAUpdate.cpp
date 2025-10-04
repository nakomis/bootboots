#include "OTAUpdate.h"
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <Preferences.h>

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
    _client = nullptr;  // Will be allocated when needed for HTTP
    _secureClient = new WiFiClientSecure();
    _secureClient->setInsecure(); // Skip certificate validation for simplicity
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
    if (_client) {
        delete _client;
        _client = nullptr;
    }
    if (_secureClient) {
        delete _secureClient;
        _secureClient = nullptr;
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
    
    LOG_I("Starting OTA service...");
    // Start OTA service
    ArduinoOTA.begin();
    LOG_I("Started OTA service...");
    
    // // Initialize mDNS for discovery
    // if (!MDNS.begin(hostname)) {
    //     SDLogger::getInstance().errorf("Error setting up mDNS responder");
    // } else {
    //     MDNS.addService("http", "tcp", 80);
    //     MDNS.addService("ota", "tcp", 3232);
    //     SDLogger::getInstance().infof("mDNS responder started: %s.local", hostname);
    // }
    
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

    // Disable SD file logging to free memory during OTA
    SDLogger::getInstance().setFileLoggingEnabled(false);
    Serial.println("SD file logging disabled for OTA - using serial only");

    _httpUpdateInProgress = true;
    _updating = true;
    _progress = 0;
    _status = "Connecting to firmware server...";

    // Configure HTTP client - use secure client for HTTPS
    String url = String(firmwareURL);
    if (url.startsWith("https://")) {
        SDLogger::getInstance().infof("Using HTTPS secure connection");
        _httpClient->begin(*_secureClient, firmwareURL);
    } else {
        SDLogger::getInstance().infof("Using HTTP insecure connection");
        _httpClient->begin(firmwareURL);
    }
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
        SDLogger::getInstance().setFileLoggingEnabled(true);
        // BLE was deinitialized - must reboot to restore connectivity
        Serial.println("OTA failed - rebooting to restore BLE connectivity...");
        delay(2000);
        ESP.restart();
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
        SDLogger::getInstance().setFileLoggingEnabled(true);
        // BLE was deinitialized - must reboot to restore connectivity
        Serial.println("OTA failed - rebooting to restore BLE connectivity...");
        delay(2000);
        ESP.restart();
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
        SDLogger::getInstance().setFileLoggingEnabled(true);
        // BLE was aialized - must reboot to restore connectivity
        Serial.println("OTA failed - rebooting to restore BLE connectivity...");
        delay(2000);
        ESP.restart();
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
                SDLogger::getInstance().setFileLoggingEnabled(true);
                // BLE was deinitialized - must reboot to restore connectivity
                Serial.println("OTA failed - rebooting to restore BLE connectivity...");
                delay(2000);
                ESP.restart();
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
        SDLogger::getInstance().setFileLoggingEnabled(true);
        // BLE was deinitialized - must reboot to restore connectivity
        Serial.println("OTA failed - rebooting to restore BLE connectivity...");
        delay(2000);
        ESP.restart();
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

// ============================================================================
// Two-Stage OTA: SD Card Download + Boot Flash
// ============================================================================

#define FIRMWARE_FILE "/firmware_update.bin"
#define OTA_BUFFER_SIZE 512

bool OTAUpdate::hasPendingUpdate() {
    Preferences prefs;
    prefs.begin("ota", true);  // read-only
    bool pending = prefs.getBool("pending", false);
    prefs.end();
    return pending;
}

bool OTAUpdate::downloadToSD(const char* firmwareURL) {
    if (_updating || _httpUpdateInProgress) {
        SDLogger::getInstance().warnf("Update already in progress");
        return false;
    }

    SDLogger::getInstance().infof("Starting two-stage OTA: downloading to SD card from %s", firmwareURL);
    Serial.println("[OTA] Starting two-stage OTA");

    // Disable SD file logging to free memory during download
    Serial.println("[OTA] Disabling SD file logging...");
    SDLogger::getInstance().setFileLoggingEnabled(false);
    Serial.println("[OTA] SD file logging disabled");

    Serial.println("[OTA] Setting state variables...");
    _httpUpdateInProgress = true;
    _updating = true;
    _progress = 0;
    _status = "Downloading to SD card...";
    Serial.println("[OTA] State variables set");

    // Configure HTTP client
    Serial.println("[OTA] Configuring HTTP client...");
    String url = String(firmwareURL);
    Serial.println("[OTA] URL string created");

    if (url.startsWith("https://")) {
        Serial.println("[OTA] Using HTTPS");
        SDLogger::getInstance().infof("Using HTTPS secure connection");
        _httpClient->begin(*_secureClient, firmwareURL);
    } else {
        Serial.println("[OTA] Using HTTP");
        SDLogger::getInstance().infof("Using HTTP connection");
        // Allocate WiFiClient if not already allocated
        if (!_client) {
            Serial.println("[OTA] Allocating WiFiClient...");
            _client = new WiFiClient();
            Serial.println("[OTA] WiFiClient allocated");
        }
        Serial.println("[OTA] Calling HTTPClient.begin()...");
        _httpClient->begin(*_client, firmwareURL);
        Serial.println("[OTA] HTTPClient.begin() complete");
    }

    Serial.println("[OTA] Setting timeout...");
    _httpClient->setTimeout(30000);  // 30 second timeout
    Serial.println("[OTA] Timeout set");

    // Make HTTP GET request
    Serial.println("[OTA] Sending HTTP GET request...");
    int httpCode = _httpClient->GET();
    Serial.printf("[OTA] HTTP GET returned code: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        SDLogger::getInstance().errorf("HTTP GET failed: %d", httpCode);
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _httpUpdateInProgress = false;
        _updating = false;
        Serial.println("Download failed - rebooting to restore connectivity...");
        delay(2000);
        ESP.restart();
        return false;
    }

    // Get firmware size
    size_t firmwareSize = _httpClient->getSize();
    if (firmwareSize == 0 || firmwareSize == (size_t)-1) {
        SDLogger::getInstance().errorf("Invalid firmware size: %d", firmwareSize);
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _httpUpdateInProgress = false;
        _updating = false;
        Serial.println("Invalid firmware size - rebooting to restore connectivity...");
        delay(2000);
        ESP.restart();
        return false;
    }

    Serial.printf("Firmware size: %d bytes\n", firmwareSize);
    _totalSize = firmwareSize;

    // Open SD card file for writing
    File file = SD_MMC.open(FIRMWARE_FILE, FILE_WRITE);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open SD card file for writing");
        _httpClient->end();
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _httpUpdateInProgress = false;
        _updating = false;
        Serial.println("SD card error - rebooting to restore connectivity...");
        delay(2000);
        ESP.restart();
        return false;
    }

    // Download firmware to SD card in chunks
    WiFiClient* stream = _httpClient->getStreamPtr();
    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;

    Serial.println("Downloading firmware to SD card...");
    while (_httpClient->connected() && written < firmwareSize) {
        size_t available = stream->available();
        if (available) {
            size_t bytesToRead = min(available, sizeof(buffer));
            size_t bytesRead = stream->readBytes(buffer, bytesToRead);

            if (bytesRead > 0) {
                size_t bytesWritten = file.write(buffer, bytesRead);
                if (bytesWritten != bytesRead) {
                    SDLogger::getInstance().errorf("SD write failed: wrote %d of %d bytes", bytesWritten, bytesRead);
                    file.close();
                    SD_MMC.remove(FIRMWARE_FILE);
                    _httpClient->end();
                    SDLogger::getInstance().setFileLoggingEnabled(true);
                    _httpUpdateInProgress = false;
                    _updating = false;
                    Serial.println("SD write error - rebooting to restore connectivity...");
                    delay(2000);
                    ESP.restart();
                    return false;
                }

                written += bytesRead;
                _downloadedSize = written;
                _progress = (written * 100) / firmwareSize;

                // Log progress every 10%
                if (_progress >= lastProgress + 10) {
                    Serial.printf("Download progress: %d%% (%d/%d bytes)\n", _progress, written, firmwareSize);
                    lastProgress = _progress;
                }
            }
        }
        delay(1);  // Yield to watchdog
    }

    Serial.println("[OTA] Closing file...");
    file.close();
    Serial.println("[OTA] File closed");

    Serial.println("[OTA] Ending HTTP client...");
    _httpClient->end();
    Serial.println("[OTA] HTTP client ended");
    Serial.flush();

    if (written != firmwareSize) {
        SDLogger::getInstance().errorf("Download incomplete: %d of %d bytes", written, firmwareSize);
        SD_MMC.remove(FIRMWARE_FILE);
        SDLogger::getInstance().setFileLoggingEnabled(true);
        _httpUpdateInProgress = false;
        _updating = false;
        Serial.println("Download incomplete - rebooting to restore connectivity...");
        delay(2000);
        ESP.restart();
        return false;
    }

    Serial.printf("[OTA] Download complete: %d bytes written to SD card\n", written);
    Serial.flush();

    // Set pending OTA flag in NVS
    Serial.println("[OTA] Opening NVS preferences...");
    Preferences prefs;
    Serial.println("[OTA] Calling prefs.begin()...");
    prefs.begin("ota", false);  // read-write
    Serial.println("[OTA] NVS opened, setting pending flag...");
    prefs.putBool("pending", true);
    Serial.println("[OTA] Setting size...");
    prefs.putUInt("size", firmwareSize);
    Serial.println("[OTA] Closing prefs...");
    prefs.end();
    Serial.println("[OTA] NVS preferences closed");
    Serial.flush();

    Serial.println("[OTA] OTA download complete - rebooting to flash firmware...");
    SDLogger::getInstance().infof("OTA download complete, rebooting to flash firmware");
    Serial.flush();

    delay(2000);
    Serial.println("[OTA] Restarting...");
    Serial.flush();
    ESP.restart();

    return true;
}

bool OTAUpdate::flashFromSD() {
    SDLogger::getInstance().infof("Checking for pending OTA update...");

    Preferences prefs;
    prefs.begin("ota", true);  // read-only
    bool pending = prefs.getBool("pending", false);
    size_t expectedSize = prefs.getUInt("size", 0);
    prefs.end();

    if (!pending) {
        SDLogger::getInstance().infof("No pending OTA update");
        return true;  // Not an error, just nothing to do
    }

    SDLogger::getInstance().infof("Pending OTA update found, expected size: %d bytes", expectedSize);

    // CRITICAL: Clear pending flag BEFORE attempting flash to prevent boot loops
    prefs.begin("ota", false);
    prefs.putBool("pending", false);
    prefs.end();
    SDLogger::getInstance().infof("Cleared pending OTA flag to prevent boot loop on failure");

    // Open firmware file from SD card
    File file = SD_MMC.open(FIRMWARE_FILE, FILE_READ);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open firmware file from SD card: %s", FIRMWARE_FILE);
        return false;
    }

    size_t fileSize = file.size();
    SDLogger::getInstance().infof("Firmware file size: %d bytes", fileSize);

    if (fileSize != expectedSize) {
        SDLogger::getInstance().errorf("File size mismatch! Expected %d, got %d", expectedSize, fileSize);
        file.close();
        SD_MMC.remove(FIRMWARE_FILE);
        return false;
    }

    // Begin update
    SDLogger::getInstance().infof("Starting firmware flash from SD card...");
    if (!Update.begin(fileSize)) {
        SDLogger::getInstance().errorf("Update.begin() failed: %s", Update.errorString());
        file.close();
        SD_MMC.remove(FIRMWARE_FILE);
        return false;
    }

    // Write firmware to flash
    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t written = 0;
    size_t lastProgress = 0;

    while (file.available()) {
        size_t bytesRead = file.read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            size_t bytesWritten = Update.write(buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                SDLogger::getInstance().errorf("Update.write() failed. Expected %d, wrote %d", bytesRead, bytesWritten);
                file.close();
                Update.abort();
                SD_MMC.remove(FIRMWARE_FILE);
                return false;
            }

            written += bytesWritten;
            int progress = (written * 100) / fileSize;

            // Log progress every 10%
            if (progress >= lastProgress + 10) {
                SDLogger::getInstance().infof("Flash progress: %d%% (%d/%d bytes)", progress, written, fileSize);
                lastProgress = progress;
            }
        }
    }

    file.close();

    // Finalize update
    if (!Update.end(true)) {  // true = set new firmware size
        SDLogger::getInstance().errorf("Update.end() failed: %s", Update.errorString());
        SD_MMC.remove(FIRMWARE_FILE);
        return false;
    }

    SDLogger::getInstance().infof("Firmware flash complete! Total written: %d bytes", written);

    // Clean up: remove firmware file
    SD_MMC.remove(FIRMWARE_FILE);
    SDLogger::getInstance().infof("Deleted firmware file from SD card");

    SDLogger::getInstance().infof("OTA update successful - rebooting with new firmware...");
    delay(2000);
    ESP.restart();

    return true;
}
