#ifndef CATCAM_BLUETOOTH_SERVICE_H
#define CATCAM_BLUETOOTH_SERVICE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <vector>
#include "../../SDLogger/src/SDLogger.h"
#include "../../LedController/src/LedController.h"
#include "../../CommandDispatcher/src/CommandDispatcher.h"
#include "../../../include/SystemState.h"

// BootBoots BLE Service UUID (lowercase for Web Bluetooth API compatibility)
#define BOOTBOOTS_SERVICE_UUID        "bb00b007-5af3-41c3-9689-2fc7175c1ba8"
#define STATUS_CHARACTERISTIC_UUID    "bb00b007-e90f-49fa-89c5-31e705b74d85"
#define LOGS_CHARACTERISTIC_UUID      "bb00b007-f1a2-49fa-89c5-31e705b74d86"
#define COMMAND_CHARACTERISTIC_UUID   "bb00b007-c0de-49fa-89c5-31e705b74d87"

/**
 * BLE Response Sender - Implements IResponseSender for Bluetooth transport
 * Supports chunked transfers for large data (images, logs)
 */
class BleResponseSender : public IResponseSender {
public:
    BleResponseSender(BLECharacteristic* characteristic, bool* connected);

    void sendResponse(const String& response) override;
    bool supportsChunking() const override { return true; }
    const char* getName() const override { return "BLE"; }

private:
    BLECharacteristic* _characteristic;
    bool* _connected;
};

class BootBootsBluetoothService : public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
    BootBootsBluetoothService();
    void init(const char* deviceName = "BootBoots-CatCam");
    void handle();  // Call in main loop to process deferred operations
    void updateSystemStatus(const SystemState& state);
    void setLogData(const String& logData);
    bool isConnected();
    void handleCommand(const String& command);

    // Get BLE server for sharing with other services
    BLEServer* getServer() { return pServer; }

    // Set LED controller for visual feedback during transfers
    void setLedController(LedController* led) { _ledController = led; }

    // Set command dispatcher for unified command handling
    void setCommandDispatcher(CommandDispatcher* dispatcher) { _commandDispatcher = dispatcher; }

    // Set callback for training mode changes (legacy, now handled by dispatcher)
    void setTrainingModeCallback(std::function<void(bool)> callback) { _trainingModeCallback = callback; }

    // Set callback for camera setting changes (legacy, now handled by dispatcher)
    void setCameraSettingCallback(std::function<void(const String&, int)> callback) { _cameraSettingCallback = callback; }

    // BLE Server callbacks
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;

    // BLE Characteristic callbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;
    void onRead(BLECharacteristic* pCharacteristic) override;

private:
    BLEServer* pServer;
    BLEService* pService;
    BLECharacteristic* pStatusCharacteristic;
    BLECharacteristic* pLogsCharacteristic;
    BLECharacteristic* pCommandCharacteristic;
    
    bool deviceConnected;
    volatile bool pendingConnectLog;  // Deferred logging to avoid stack overflow in BLE callback
    String currentStatusJson;
    String currentLogsData;

    // Deferred command processing (avoid stack overflow in BLE callback)
    static const int MAX_PENDING_CMD_SIZE = 512;
    char _pendingCommandBuffer[MAX_PENDING_CMD_SIZE];
    volatile bool _hasPendingCommand;
    volatile bool _pendingDisconnect;  // Deferred disconnect handling
    LedController* _ledController = nullptr;  // Optional LED for visual feedback
    CommandDispatcher* _commandDispatcher = nullptr;  // Command dispatcher for unified handling
    BleResponseSender* _responseSender = nullptr;  // Response sender for dispatcher
    std::function<void(bool)> _trainingModeCallback = nullptr;  // Callback for training mode changes
    std::function<void(const String&, int)> _cameraSettingCallback = nullptr;  // Callback for camera setting changes

    // Helper methods
    String formatSystemStatusJson(const SystemState& state);
    String getLatestLogEntries(int maxEntries = 50);
    void processCommand(const String& command);
    void sendResponse(const String& response);

    // Image transfer methods
    std::vector<String> listImages();
    void sendImageList();
    void sendImage(const String& filename);
    void sendImageMetadata(const String& filename);
};

#endif
