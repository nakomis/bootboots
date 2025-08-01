#ifndef CATCAM_BLUETOOTH_SERVICE_H
#define CATCAM_BLUETOOTH_SERVICE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include "../../SDLogger/src/SDLogger.h"

// BootBoots BLE Service UUID
#define BOOTBOOTS_SERVICE_UUID        "BB00B007-5AF3-41C3-9689-2FC7175C1BA8"
#define STATUS_CHARACTERISTIC_UUID    "BB00B007-E90F-49FA-89C5-31E705B74D85"
#define LOGS_CHARACTERISTIC_UUID      "BB00B007-F1A2-49FA-89C5-31E705B74D86"
#define COMMAND_CHARACTERISTIC_UUID   "BB00B007-C0DE-49FA-89C5-31E705B74D87"

// Forward declarations
struct SystemState;

class BootBootsBluetoothService : public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
    BootBootsBluetoothService();
    void init(const char* deviceName = "BootBoots-CatCam");
    void updateSystemStatus(const SystemState& state);
    void setLogData(const String& logData);
    bool isConnected();
    void handleCommand(const String& command);
    
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
    String currentStatusJson;
    String currentLogsData;
    
    // Helper methods
    String formatSystemStatusJson(const SystemState& state);
    String getLatestLogEntries(int maxEntries = 50);
    void processCommand(const String& command);
    void sendResponse(const String& response);
};

#endif
