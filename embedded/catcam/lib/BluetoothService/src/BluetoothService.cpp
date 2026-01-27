#include "BluetoothService.h"
#include <algorithm>

// External reference to systemState defined in main.cpp
extern SystemState systemState;

// External photo capture function from main.cpp (returns new filename)
extern String captureAndPostPhoto();

BootBootsBluetoothService::BootBootsBluetoothService()
    : pServer(nullptr), pService(nullptr), pStatusCharacteristic(nullptr),
      pLogsCharacteristic(nullptr), pCommandCharacteristic(nullptr),
      deviceConnected(false), pendingConnectLog(false), _hasPendingCommand(false),
      _pendingDisconnect(false) {
    memset(_pendingCommandBuffer, 0, MAX_PENDING_CMD_SIZE);
}

void BootBootsBluetoothService::init(const char* deviceName) {
    SDLogger::getInstance().infof("Initializing BootBoots Bluetooth Service...");
    
    // Initialize BLE
    BLEDevice::init(deviceName);
    LOG_DF("BLE Device initialized with name: %s", deviceName);
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    LOG_DF("BLE Server created");
    pServer->setCallbacks(this);
    LOG_DF("BLE Server callbacks set");
    
    // Create BLE Service
    pService = pServer->createService(BOOTBOOTS_SERVICE_UUID);
    LOG_DF("BLE Service created with UUID: %s", BOOTBOOTS_SERVICE_UUID);
    
    // Create Status Characteristic (Read/Notify)
    pStatusCharacteristic = pService->createCharacteristic(
        STATUS_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    LOG_DF("Status Characteristic created with UUID: %s", STATUS_CHARACTERISTIC_UUID);
    pStatusCharacteristic->setCallbacks(this);
    LOG_DF("Status Characteristic callbacks set");
    pStatusCharacteristic->addDescriptor(new BLE2902());
    LOG_DF("Status Characteristic descriptor added");
    
    // Create Logs Characteristic (Read)
    pLogsCharacteristic = pService->createCharacteristic(
        LOGS_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pLogsCharacteristic->setCallbacks(this);
    LOG_DF("Logs Characteristic created with UUID: %s", LOGS_CHARACTERISTIC_UUID);

    // Create Command Characteristic (Write/Notify for request/response)
    pCommandCharacteristic = pService->createCharacteristic(
        COMMAND_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCommandCharacteristic->setCallbacks(this);
    pCommandCharacteristic->addDescriptor(new BLE2902());
    LOG_DF("Command Characteristic created with UUID: %s", COMMAND_CHARACTERISTIC_UUID);
    
    // Start the service
    pService->start();
    LOG_DF("BLE Service started");

    // Add service UUID to advertising (but don't start advertising yet)
    // Advertising will be started after all services are initialized
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    LOG_DF("BLE Advertising instance obtained");
    pAdvertising->addServiceUUID(BOOTBOOTS_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    LOG_DF("BLE Service UUID added to advertising");

    SDLogger::getInstance().infof("BootBoots Bluetooth Service initialized - Device: %s", deviceName);
    SDLogger::getInstance().infof("Service UUID: %s", BOOTBOOTS_SERVICE_UUID);
}

void BootBootsBluetoothService::updateSystemStatus(const SystemState& state) {
    currentStatusJson = formatSystemStatusJson(state);
    
    if (deviceConnected && pStatusCharacteristic) {
        pStatusCharacteristic->setValue(currentStatusJson.c_str());
        pStatusCharacteristic->notify();
    }
}

String BootBootsBluetoothService::formatSystemStatusJson(const SystemState& state) {
    DynamicJsonDocument doc(1024);
    
    unsigned long uptime = millis() - state.systemStartTime;
    
    doc["device"] = "BootBoots-CatCam";
    doc["timestamp"] = millis();
    doc["uptime_seconds"] = uptime / 1000;
    doc["system"]["initialized"] = state.initialized;
    doc["system"]["camera_ready"] = state.cameraReady;
    doc["system"]["wifi_connected"] = state.wifiConnected;
    doc["system"]["sd_card_ready"] = state.sdCardReady;
    doc["system"]["i2c_ready"] = state.i2cReady;
    doc["system"]["atomizer_enabled"] = state.atomizerEnabled;
    
    doc["statistics"]["total_detections"] = state.totalDetections;
    doc["statistics"]["boots_detections"] = state.bootsDetections;
    doc["statistics"]["atomizer_activations"] = state.atomizerActivations;
    doc["statistics"]["false_positives_avoided"] = state.falsePositivesAvoided;

    doc["system"]["training_mode"] = state.trainingMode;
    
    doc["timing"]["last_detection"] = state.lastDetection;
    doc["timing"]["last_status_report"] = state.lastStatusReport;
    
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

void BootBootsBluetoothService::setLogData(const String& logData) {
    currentLogsData = logData;
}

String BootBootsBluetoothService::getLatestLogEntries(int maxEntries) {
    // Get recent log entries from SDLogger
    return SDLogger::getInstance().getRecentLogEntries(maxEntries);
}

bool BootBootsBluetoothService::isConnected() {
    return deviceConnected;
}

void BootBootsBluetoothService::handle() {
    // Process deferred operations to avoid stack overflow in BLE callbacks
    if (pendingConnectLog) {
        pendingConnectLog = false;
        SDLogger::getInstance().infof("Bluetooth client connected");
    }

    // Process deferred disconnect (from onDisconnect callback)
    if (_pendingDisconnect) {
        _pendingDisconnect = false;
        SDLogger::getInstance().infof("BluetoothService client disconnected");
        delay(500);
        if (pServer) {
            pServer->startAdvertising();
        }
        SDLogger::getInstance().infof("Bluetooth advertising restarted");
    }

    // Process deferred command (from onWrite callback)
    if (_hasPendingCommand) {
        _hasPendingCommand = false;

        // Copy to local String now that we're in main loop context with full stack
        String command = String(_pendingCommandBuffer);
        _pendingCommandBuffer[0] = '\0';

        SDLogger::getInstance().infof("Bluetooth command received: %s", command.c_str());
        processCommand(command);
    }
}

void BootBootsBluetoothService::onConnect(BLEServer* pServer) {
    deviceConnected = true;
    // Defer logging to handle() - BLE callback has limited stack (BTC_TASK)
    pendingConnectLog = true;
}

void BootBootsBluetoothService::onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    // Defer logging and advertising restart to handle() - BLE callback has limited stack (BTC_TASK)
    _pendingDisconnect = true;
}

void BootBootsBluetoothService::onWrite(BLECharacteristic* pCharacteristic) {
    // Defer command processing to handle() to avoid stack overflow in BLE callback (BTC_TASK has ~3.5KB stack)
    if (pCharacteristic == pCommandCharacteristic) {
        uint8_t* data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();

        if (data && len > 0 && len < MAX_PENDING_CMD_SIZE - 1) {
            memcpy(_pendingCommandBuffer, data, len);
            _pendingCommandBuffer[len] = '\0';
            _hasPendingCommand = true;
        }
    }
}

void BootBootsBluetoothService::onRead(BLECharacteristic* pCharacteristic) {
    // IMPORTANT: This runs in BTC_TASK with limited stack (~3.5KB)
    // Do NOT log or perform heavy operations here!
    if (pCharacteristic == pStatusCharacteristic) {
        // Just set the cached status - no logging
        pCharacteristic->setValue(currentStatusJson.c_str());
    } else if (pCharacteristic == pLogsCharacteristic) {
        // Return cached logs data (logs are now retrieved via command mechanism)
        // Don't read from SD card here - too heavy for BLE callback
        pCharacteristic->setValue(currentLogsData.c_str());
    }
}

void BootBootsBluetoothService::processCommand(const String& command) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, command);

    LOG_DF("Processing command: %s", command.c_str());
    
    if (error) {
        SDLogger::getInstance().warnf("Invalid JSON command: %s", command.c_str());
        return;
    }
    
    String cmd = doc["command"];
    
    if (cmd == "get_status") {
        // Status is automatically sent via notifications
        SDLogger::getInstance().infof("Status request via command");
    } else if (cmd == "get_logs" || cmd == "request_logs") {
        // Get logs and send via command characteristic notification, one line at a time
        int entries = doc["entries"] | -1;  // -1 means all entries

        SDLogger::getInstance().infof("Log request via command: %d entries requested", entries);

        int lineCount = 0;

        // Process each log entry and send as a chunk
        SDLogger::getInstance().processRecentLogEntries(entries, [this, &lineCount](const String& logLine) {
            lineCount++;

            // Create a chunk message for this log line
            DynamicJsonDocument chunkDoc(512);
            chunkDoc["type"] = "log_chunk";
            chunkDoc["chunk"] = lineCount;
            chunkDoc["data"] = logLine;

            String chunkJson;
            serializeJson(chunkDoc, chunkJson);

            sendResponse(chunkJson);

            // Small delay between chunks to avoid overwhelming BLE
            delay(50);
        });

        // Send completion message
        DynamicJsonDocument completeDoc(128);
        completeDoc["type"] = "logs_complete";
        completeDoc["total_chunks"] = lineCount;

        String completeJson;
        serializeJson(completeDoc, completeJson);
        sendResponse(completeJson);
        SDLogger::getInstance().infof("Log transfer complete: %d lines sent", lineCount);
    } else if (cmd == "ping") {
        DynamicJsonDocument response(256);
        response["response"] = "pong";
        response["timestamp"] = millis();
        String responseStr;
        serializeJson(response, responseStr);
        SDLogger::getInstance().infof("Ping received, sending pong");
        sendResponse(responseStr);
    } else if (cmd == "list_images") {
        SDLogger::getInstance().infof("Image list request via command");
        sendImageList();
    } else if (cmd == "get_image") {
        String filename = doc["filename"] | "";
        if (filename.length() > 0) {
            SDLogger::getInstance().infof("Image request via command: %s", filename.c_str());
            sendImage(filename);
        } else {
            SDLogger::getInstance().warnf("get_image command missing filename");
            DynamicJsonDocument errorDoc(128);
            errorDoc["type"] = "error";
            errorDoc["message"] = "Missing filename parameter";
            String errorJson;
            serializeJson(errorDoc, errorJson);
            sendResponse(errorJson);
        }
    } else if (cmd == "get_image_metadata") {
        String filename = doc["filename"] | "";
        if (filename.length() > 0) {
            SDLogger::getInstance().infof("Metadata request via command: %s", filename.c_str());
            sendImageMetadata(filename);
        } else {
            SDLogger::getInstance().warnf("get_image_metadata command missing filename");
            DynamicJsonDocument errorDoc(128);
            errorDoc["type"] = "error";
            errorDoc["message"] = "Missing filename parameter";
            String errorJson;
            serializeJson(errorDoc, errorJson);
            sendResponse(errorJson);
        }
    } else if (cmd == "take_photo") {
        SDLogger::getInstance().infof("Take photo request via Bluetooth");
        // Send acknowledgment first
        DynamicJsonDocument ackDoc(128);
        ackDoc["type"] = "photo_started";
        ackDoc["message"] = "Capturing photo...";
        String ackJson;
        serializeJson(ackDoc, ackJson);
        sendResponse(ackJson);

        // Capture the photo (returns new filename)
        String newFilename = captureAndPostPhoto();

        // Send completion with filename
        DynamicJsonDocument completeDoc(256);
        completeDoc["type"] = "photo_complete";
        completeDoc["message"] = "Photo captured and saved";
        completeDoc["filename"] = newFilename;
        String completeJson;
        serializeJson(completeDoc, completeJson);
        sendResponse(completeJson);
    } else if (cmd == "get_settings") {
        SDLogger::getInstance().infof("Get settings request via Bluetooth");
        DynamicJsonDocument response(256);
        response["type"] = "settings";
        response["training_mode"] = systemState.trainingMode;
        String responseStr;
        serializeJson(response, responseStr);
        sendResponse(responseStr);
    } else if (cmd == "set_setting") {
        String setting = doc["setting"] | "";
        if (setting == "training_mode") {
            bool value = doc["value"] | false;
            SDLogger::getInstance().infof("Setting training_mode to %s via Bluetooth", value ? "true" : "false");
            systemState.trainingMode = value;

            // Notify callback to persist the setting
            if (_trainingModeCallback) {
                _trainingModeCallback(value);
            }

            // Send confirmation
            DynamicJsonDocument response(256);
            response["type"] = "setting_updated";
            response["setting"] = "training_mode";
            response["value"] = value;
            String responseStr;
            serializeJson(response, responseStr);
            sendResponse(responseStr);
        } else {
            SDLogger::getInstance().warnf("Unknown setting: %s", setting.c_str());
            DynamicJsonDocument errorDoc(128);
            errorDoc["type"] = "error";
            errorDoc["message"] = "Unknown setting";
            String errorJson;
            serializeJson(errorDoc, errorJson);
            sendResponse(errorJson);
        }
    } else {
        SDLogger::getInstance().warnf("Unknown command: %s", cmd.c_str());
    }
}

void BootBootsBluetoothService::sendResponse(const String& response) {
    if (deviceConnected && pCommandCharacteristic) {
        pCommandCharacteristic->setValue(response.c_str());
        pCommandCharacteristic->notify();
    }
}

std::vector<String> BootBootsBluetoothService::listImages() {
    std::vector<String> imageFiles;

    File dir = SD_MMC.open("/images");
    if (!dir || !dir.isDirectory()) {
        SDLogger::getInstance().warnf("Failed to open /images directory");
        return imageFiles;
    }

    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        if (name.endsWith(".jpg")) {
            imageFiles.push_back(name);
        }
        entry.close();
    }
    dir.close();

    // Sort alphabetically (newest last due to timestamp naming)
    std::sort(imageFiles.begin(), imageFiles.end());

    return imageFiles;
}

void BootBootsBluetoothService::sendImageList() {
    std::vector<String> images = listImages();
    size_t totalImages = images.size();

    SDLogger::getInstance().infof("Sending image list: %d images", totalImages);

    // Send each image filename as a separate chunk
    for (size_t i = 0; i < totalImages; i++) {
        DynamicJsonDocument chunkDoc(256);
        chunkDoc["type"] = "image_list_chunk";
        chunkDoc["chunk"] = i;
        chunkDoc["total"] = totalImages;
        chunkDoc["filename"] = images[i];

        String chunkJson;
        serializeJson(chunkDoc, chunkJson);
        sendResponse(chunkJson);

        delay(30);  // Small delay between chunks
    }

    // Send completion message
    DynamicJsonDocument completeDoc(128);
    completeDoc["type"] = "image_list_complete";
    completeDoc["count"] = totalImages;

    String completeJson;
    serializeJson(completeDoc, completeJson);
    sendResponse(completeJson);

    SDLogger::getInstance().infof("Image list transfer complete: %d images", totalImages);
}

void BootBootsBluetoothService::sendImage(const String& filename) {
    // Construct full path
    String filepath = "/images/" + filename;

    File file = SD_MMC.open(filepath.c_str(), FILE_READ);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open image file: %s", filepath.c_str());
        DynamicJsonDocument errorDoc(128);
        errorDoc["type"] = "error";
        errorDoc["message"] = "File not found";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        sendResponse(errorJson);
        return;
    }

    size_t fileSize = file.size();
    SDLogger::getInstance().infof("Sending image: %s (%d bytes)", filename.c_str(), fileSize);

    // Send start notification
    DynamicJsonDocument startDoc(256);
    startDoc["type"] = "image_start";
    startDoc["filename"] = filename;
    startDoc["size"] = fileSize;
    String startJson;
    serializeJson(startDoc, startJson);
    sendResponse(startJson);
    delay(50);

    // Send image data in chunks (base64 encoded)
    // BLE MTU is typically 512 bytes, so use ~400 bytes of base64 per chunk (~300 bytes raw)
    const size_t RAW_CHUNK_SIZE = 300;
    uint8_t buffer[RAW_CHUNK_SIZE];
    int chunkIndex = 0;
    size_t totalChunks = (fileSize + RAW_CHUNK_SIZE - 1) / RAW_CHUNK_SIZE;
    bool ledGreen = true;  // Toggle between green and blue

    while (file.available()) {
        // Toggle LED between green and blue for each chunk
        if (_ledController) {
            if (ledGreen) {
                _ledController->setColor(0, 255, 0);  // Green
            } else {
                _ledController->setColor(0, 0, 255);  // Blue
            }
        }
        ledGreen = !ledGreen;
        size_t bytesRead = file.read(buffer, RAW_CHUNK_SIZE);

        // Base64 encode the chunk
        size_t encodedLen = ((bytesRead + 2) / 3) * 4 + 1;
        char* encoded = new char[encodedLen];

        // Simple base64 encoding
        static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t i = 0, j = 0;
        uint8_t arr3[3], arr4[4];
        int k = 0;

        for (size_t n = 0; n < bytesRead; n++) {
            arr3[i++] = buffer[n];
            if (i == 3) {
                arr4[0] = (arr3[0] & 0xfc) >> 2;
                arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
                arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
                arr4[3] = arr3[2] & 0x3f;
                for (i = 0; i < 4; i++) {
                    encoded[k++] = base64_chars[arr4[i]];
                }
                i = 0;
            }
        }

        if (i > 0) {
            for (size_t n = i; n < 3; n++) arr3[n] = 0;
            arr4[0] = (arr3[0] & 0xfc) >> 2;
            arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
            arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
            for (size_t n = 0; n < i + 1; n++) {
                encoded[k++] = base64_chars[arr4[n]];
            }
            while (i++ < 3) {
                encoded[k++] = '=';
            }
        }
        encoded[k] = '\0';

        // Send chunk
        DynamicJsonDocument chunkDoc(600);
        chunkDoc["type"] = "image_chunk";
        chunkDoc["chunk"] = chunkIndex;
        chunkDoc["total"] = totalChunks;
        chunkDoc["data"] = encoded;

        String chunkJson;
        serializeJson(chunkDoc, chunkJson);
        sendResponse(chunkJson);

        delete[] encoded;
        chunkIndex++;

        // Small delay between chunks
        delay(30);
    }

    file.close();

    // Turn off LED after transfer
    if (_ledController) {
        _ledController->off();
    }

    // Send completion notification
    DynamicJsonDocument endDoc(128);
    endDoc["type"] = "image_complete";
    endDoc["filename"] = filename;
    endDoc["chunks"] = chunkIndex;
    String endJson;
    serializeJson(endDoc, endJson);
    sendResponse(endJson);

    SDLogger::getInstance().infof("Image transfer complete: %d chunks sent", chunkIndex);
}

void BootBootsBluetoothService::sendImageMetadata(const String& filename) {
    // Construct the .txt file path from the .jpg filename
    String txtFilename = filename;
    if (txtFilename.endsWith(".jpg")) {
        txtFilename = txtFilename.substring(0, txtFilename.length() - 4) + ".txt";
    } else {
        txtFilename = txtFilename + ".txt";
    }

    String filepath = "/images/" + txtFilename;

    File file = SD_MMC.open(filepath.c_str(), FILE_READ);
    if (!file) {
        SDLogger::getInstance().warnf("Metadata file not found: %s", filepath.c_str());
        DynamicJsonDocument errorDoc(256);
        errorDoc["type"] = "metadata_result";
        errorDoc["filename"] = filename;
        errorDoc["found"] = false;
        errorDoc["content"] = "";
        String errorJson;
        serializeJson(errorDoc, errorJson);
        sendResponse(errorJson);
        return;
    }

    // Read the entire .txt file (should be small JSON)
    String content = file.readString();
    file.close();

    SDLogger::getInstance().infof("Sending metadata for: %s (%d bytes)", filename.c_str(), content.length());

    // Send the metadata content
    DynamicJsonDocument resultDoc(1024);
    resultDoc["type"] = "metadata_result";
    resultDoc["filename"] = filename;
    resultDoc["found"] = true;
    resultDoc["content"] = content;

    String resultJson;
    serializeJson(resultDoc, resultJson);
    sendResponse(resultJson);

    SDLogger::getInstance().infof("Metadata transfer complete for: %s", filename.c_str());
}
