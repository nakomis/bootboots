#ifndef BLUETOOTH_OTA_H
#define BLUETOOTH_OTA_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "../../SDLogger/src/SDLogger.h"
#include "../../OTAUpdate/src/OTAUpdate.h"

// Service and Characteristic UUIDs for OTA
#define OTA_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define OTA_COMMAND_CHAR_UUID   "87654321-4321-4321-4321-cba987654321"
#define OTA_STATUS_CHAR_UUID    "11111111-2222-3333-4444-555555555555"

struct OTACommand {
    String action;
    String firmware_url;
    String version;
};

struct OTAStatus {
    String status;
    String message;
    int progress;
    String version;
};

class BluetoothOTACallbacks : public BLECharacteristicCallbacks {
public:
    BluetoothOTACallbacks(class BluetoothOTA* parent) : _parent(parent) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;
    
private:
    class BluetoothOTA* _parent;
};

class BluetoothOTA {
public:
    BluetoothOTA();
    ~BluetoothOTA();
    
    bool init(const char* deviceName = "BootBoots-CatCam");
    void handle();
    bool isConnected();
    void sendStatusUpdate(const String& status, const String& message, int progress = 0);
    void handleOTACommand(const String& commandJson);
    
    // OTA Update integration
    void setOTAUpdate(OTAUpdate* otaUpdate) { _otaUpdate = otaUpdate; }
    
private:
    BLEServer* _pServer;
    BLEService* _pService;
    BLECharacteristic* _pCommandCharacteristic;
    BLECharacteristic* _pStatusCharacteristic;
    BluetoothOTACallbacks* _pCallbacks;
    OTAUpdate* _otaUpdate;
    
    bool _initialized;
    bool _deviceConnected;
    String _deviceName;
    
    // Helper methods
    OTACommand parseCommand(const String& json);
    String createStatusJson(const String& status, const String& message, int progress = 0);
    void processOTAUpdate(const OTACommand& command);
    
    // Server callbacks
    class ServerCallbacks : public BLEServerCallbacks {
    public:
        ServerCallbacks(BluetoothOTA* parent) : _parent(parent) {}
        void onConnect(BLEServer* pServer) override;
        void onDisconnect(BLEServer* pServer) override;
        
    private:
        BluetoothOTA* _parent;
    };
    
    ServerCallbacks* _pServerCallbacks;
};

#endif // BLUETOOTH_OTA_H
