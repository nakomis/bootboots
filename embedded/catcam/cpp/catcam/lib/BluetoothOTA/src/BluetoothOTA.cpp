#include "BluetoothOTA.h"
#include <ArduinoJson.h>
#include "../../../include/version.h"

BluetoothOTA::BluetoothOTA() {
    _pServer = nullptr;
    _pService = nullptr;
    _pCommandCharacteristic = nullptr;
    _pStatusCharacteristic = nullptr;
    _pCallbacks = nullptr;
    _pServerCallbacks = nullptr;
    _otaUpdate = nullptr;
    _initialized = false;
    _deviceConnected = false;
    _deviceName = "BootBoots-CatCam";
}

BluetoothOTA::~BluetoothOTA() {
    if (_pCallbacks) delete _pCallbacks;
    if (_pServerCallbacks) delete _pServerCallbacks;
}

bool BluetoothOTA::init(const char* deviceName) {
    if (_initialized) {
        SDLogger::getInstance().warnf("Bluetooth OTA already initialized");
        return true;
    }
    
    _deviceName = String(deviceName);
    
    SDLogger::getInstance().infof("Initializing Bluetooth OTA service...");
    
    // Initialize BLE
    BLEDevice::init(_deviceName.c_str());
    
    // Create BLE Server
    _pServer = BLEDevice::createServer();
    if (!_pServer) {
        SDLogger::getInstance().errorf("Failed to create BLE server");
        return false;
    }
    
    // Set server callbacks
    _pServerCallbacks = new ServerCallbacks(this);
    _pServer->setCallbacks(_pServerCallbacks);
    
    // Create BLE Service
    _pService = _pServer->createService(OTA_SERVICE_UUID);
    if (!_pService) {
        SDLogger::getInstance().errorf("Failed to create BLE service");
        return false;
    }
    
    // Create Command Characteristic (Write)
    _pCommandCharacteristic = _pService->createCharacteristic(
        OTA_COMMAND_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    
    if (!_pCommandCharacteristic) {
        SDLogger::getInstance().errorf("Failed to create command characteristic");
        return false;
    }
    
    // Set command characteristic callbacks
    _pCallbacks = new BluetoothOTACallbacks(this);
    _pCommandCharacteristic->setCallbacks(_pCallbacks);
    
    // Create Status Characteristic (Read/Notify)
    _pStatusCharacteristic = _pService->createCharacteristic(
        OTA_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    
    if (!_pStatusCharacteristic) {
        SDLogger::getInstance().errorf("Failed to create status characteristic");
        return false;
    }
    
    // Add descriptor for notifications
    _pStatusCharacteristic->addDescriptor(new BLE2902());
    
    // Start the service
    _pService->start();

    // Add service UUID to advertising (but don't start advertising yet)
    // Advertising will be started in main.cpp after all services are initialized
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(OTA_SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);

    _initialized = true;

    SDLogger::getInstance().infof("Bluetooth OTA service initialized successfully");
    SDLogger::getInstance().infof("Device name: %s", _deviceName.c_str());
    SDLogger::getInstance().infof("Service UUID: %s", OTA_SERVICE_UUID);
    
    // Send initial status
    sendStatusUpdate("ready", "Bluetooth OTA service ready");
    
    return true;
}

void BluetoothOTA::handle() {
    if (!_initialized) return;
    
    // Handle disconnection
    if (!_deviceConnected && _pServer->getConnectedCount() == 0) {
        delay(500); // Give the bluetooth stack time to get ready
        _pServer->startAdvertising(); // Restart advertising
    }
}

bool BluetoothOTA::isConnected() {
    return _deviceConnected && _pServer && _pServer->getConnectedCount() > 0;
}

void BluetoothOTA::sendStatusUpdate(const String& status, const String& message, int progress) {
    if (!_pStatusCharacteristic || !isConnected()) {
        return;
    }
    
    String statusJson = createStatusJson(status, message, progress);
    _pStatusCharacteristic->setValue(statusJson.c_str());
    _pStatusCharacteristic->notify();
    
    SDLogger::getInstance().infof("Sent status update: %s", statusJson.c_str());
}

void BluetoothOTA::handleOTACommand(const String& commandJson) {
    SDLogger::getInstance().infof("Received OTA command: %s", commandJson.c_str());
    
    OTACommand command = parseCommand(commandJson);
    
    if (command.action == "ota_update") {
        processOTAUpdate(command);
    } else if (command.action == "get_status") {
        // Send current status
        if (_otaUpdate) {
            String status = _otaUpdate->getStatus();
            int progress = _otaUpdate->getProgress();
            sendStatusUpdate("status", status, progress);
        } else {
            sendStatusUpdate("status", "OTA service not available");
        }
    } else if (command.action == "cancel_update") {
        if (_otaUpdate) {
            _otaUpdate->cancelUpdate();
            sendStatusUpdate("cancelled", "OTA update cancelled");
        }
    } else {
        sendStatusUpdate("error", "Unknown command: " + command.action);
    }
}

OTACommand BluetoothOTA::parseCommand(const String& json) {
    OTACommand command;
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        SDLogger::getInstance().errorf("Failed to parse command JSON: %s", error.c_str());
        command.action = "error";
        return command;
    }
    
    command.action = doc["action"].as<String>();
    command.firmware_url = doc["firmware_url"].as<String>();
    command.version = doc["version"].as<String>();
    
    return command;
}

String BluetoothOTA::createStatusJson(const String& status, const String& message, int progress) {
    DynamicJsonDocument doc(512);
    
    doc["status"] = status;
    doc["message"] = message;
    doc["progress"] = progress;
    doc["version"] = FIRMWARE_VERSION; // From version.h
    
    String json;
    serializeJson(doc, json);
    return json;
}

void BluetoothOTA::processOTAUpdate(const OTACommand& command) {
    if (!_otaUpdate) {
        sendStatusUpdate("error", "OTA update service not available");
        return;
    }

    if (command.firmware_url.isEmpty()) {
        sendStatusUpdate("error", "No firmware URL provided");
        return;
    }

    SDLogger::getInstance().infof("Starting OTA update from URL: %s", command.firmware_url.c_str());
    sendStatusUpdate("starting", "Starting OTA update...");

    // Stop BLE advertising to reduce interference during OTA
    // Note: We don't deinitialize BLE because it causes hangs (see esp32-snippets #1155)
    SDLogger::getInstance().infof("Stopping BLE advertising for OTA update");
    if (_pServer) {
        _pServer->getAdvertising()->stop();
    }
    BLEDevice::stopAdvertising();

    SDLogger::getInstance().infof("Free heap before OTA: %d bytes", ESP.getFreeHeap());

    // Set up progress callback
    _otaUpdate->setUpdateCallback([](bool success, const char* error) {
        // This callback will be called when the update completes
        // Note: We can't access 'this' in a static callback, so we'll handle status in the main loop
    });

    // Start the two-stage HTTP OTA update (download to SD, then flash on next boot)
    bool started = _otaUpdate->downloadToSD(command.firmware_url.c_str());

    if (!started) {
        sendStatusUpdate("error", "Failed to start OTA download");
        SDLogger::getInstance().errorf("OTA download failed to start");
        return;
    }

    sendStatusUpdate("downloading", "Downloading firmware to SD card...", 0);
    // Note: Device will reboot automatically after download completes
}

// Server Callbacks Implementation
void BluetoothOTA::ServerCallbacks::onConnect(BLEServer* pServer) {
    _parent->_deviceConnected = true;
    SDLogger::getInstance().infof("Bluetooth client connected");
    _parent->sendStatusUpdate("connected", "Client connected to BootBoots");
}

void BluetoothOTA::ServerCallbacks::onDisconnect(BLEServer* pServer) {
    _parent->_deviceConnected = false;
    SDLogger::getInstance().infof("Bluetooth client disconnected");
}

// Characteristic Callbacks Implementation
void BluetoothOTACallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
        String command = String(value.c_str());
        _parent->handleOTACommand(command);
    }
}
