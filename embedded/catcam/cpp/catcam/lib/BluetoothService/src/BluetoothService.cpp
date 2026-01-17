#include "BluetoothService.h"

// External reference to systemState defined in main.cpp
extern SystemState systemState;

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

            chunkDoc["data"] = "MHChuck";
            sendResponse(chunkJson);
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
