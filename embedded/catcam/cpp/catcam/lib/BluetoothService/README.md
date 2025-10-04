# BluetoothService

General-purpose Bluetooth Low Energy (BLE) service for monitoring and controlling the BootBoots CatCam device.

## Purpose

BluetoothService provides a comprehensive BLE interface for mobile applications to monitor system status, retrieve logs, and send commands to the CatCam device. This service is independent of firmware updates and focuses on real-time device interaction and diagnostics.

## Features

- **System Status Monitoring**: Real-time JSON status updates with notifications
- **Log Retrieval**: Access device logs via BLE
- **Command Interface**: Send commands to control device behavior
- **Auto-Reconnection**: Automatic advertising restart on client disconnect
- **SystemState Integration**: Exposes device state including WiFi, camera, sensors, and detection statistics

## BLE Service Specification

### Service UUID
```
bb00b007-5af3-41c3-9689-2fc7175c1ba8
```

### Characteristics

#### Status Characteristic (Read/Notify)
**UUID**: `bb00b007-e90f-49fa-89c5-31e705b74d85`

Returns comprehensive system status as JSON:
```json
{
  "device": "BootBoots-CatCam",
  "timestamp": 1234567,
  "uptime_seconds": 3600,
  "system": {
    "initialized": true,
    "camera_ready": true,
    "wifi_connected": true,
    "sd_card_ready": true,
    "i2c_ready": true,
    "atomizer_enabled": true
  },
  "statistics": {
    "total_detections": 42,
    "boots_detections": 15,
    "atomizer_activations": 10,
    "false_positives_avoided": 5
  },
  "timing": {
    "last_detection": 1234000,
    "last_status_report": 1234500
  }
}
```

Automatically notifies connected clients when status changes.

#### Logs Characteristic (Read)
**UUID**: `bb00b007-f1a2-49fa-89c5-31e705b74d86`

Returns recent log entries as JSON:
```json
{
  "log_entries": 50,
  "message": "Log retrieval not yet implemented - check SD card directly",
  "timestamp": 1234567
}
```

**Note**: Full log retrieval from SD card is not yet implemented.

#### Command Characteristic (Write)
**UUID**: `bb00b007-c0de-49fa-89c5-31e705b74d87`

Accepts JSON commands:
```json
{
  "command": "get_status"
}
```

```json
{
  "command": "get_logs",
  "entries": 50
}
```

```json
{
  "command": "ping"
}
```

**Supported Commands**:
- `get_status` - Request current system status
- `get_logs` - Request log entries (specify `entries` count)
- `ping` - Health check (responds with "pong")

## Usage

### Initialization

```cpp
#include <BluetoothService.h>
#include "SystemState.h"

BootBootsBluetoothService btService;
SystemState systemState;

void setup() {
    // Initialize with custom device name
    btService.init("BootBoots-CatCam");
}

void loop() {
    // Update status when system state changes
    if (systemState.hasChanged()) {
        btService.updateSystemStatus(systemState);
    }
}
```

### Update System Status

```cpp
// Status is automatically sent to connected clients
systemState.wifiConnected = true;
systemState.totalDetections++;
btService.updateSystemStatus(systemState);
```

### Check Connection Status

```cpp
if (btService.isConnected()) {
    // Client is connected
}
```

### Set Log Data

```cpp
String logData = "Recent log entries...";
btService.setLogData(logData);
```

## Dependencies

### Internal
- **SystemState**: Device state structure (defined in `include/SystemState.h`)
- **SDLogger**: Logging functionality

### External
- ESP32 BLE Arduino library (`BLEDevice.h`, `BLEServer.h`, `BLEUtils.h`)
- ArduinoJson (for status/command serialization)

## SystemState Structure

The service depends on the `SystemState` structure from `include/SystemState.h`:

```cpp
struct SystemState {
    bool initialized;
    bool cameraReady;
    bool wifiConnected;
    bool sdCardReady;
    bool i2cReady;
    bool atomizerEnabled;

    uint32_t totalDetections;
    uint32_t bootsDetections;
    uint32_t atomizerActivations;
    uint32_t falsePositivesAvoided;

    unsigned long systemStartTime;
    unsigned long lastDetection;
    unsigned long lastStatusReport;
};
```

## Relationship to Other Components

```
Mobile App (BLE Client)
        ↓
  BluetoothService ──→ SystemState (main.cpp)
        ↓                    ↓
  Status Notifications   Device Subsystems
```

**BluetoothService** is independent of firmware updates and provides general device monitoring. For OTA updates, see **BluetoothOTA**.

## Implementation Notes

- Device name defaults to "BootBoots-CatCam"
- Uses Web Bluetooth API-compatible lowercase UUIDs
- Status notifications are sent only when a client is connected
- Automatically restarts advertising on disconnect
- External `systemState` variable must be defined in `main.cpp`

## Advertising Configuration

The service configures advertising for optimal iOS compatibility:
- Scan response enabled
- Min preferred connection interval: 0x06 and 0x12 (helps with iPhone connections)

## Web Bluetooth Compatibility

All UUIDs are lowercase for compatibility with the Web Bluetooth API, enabling web-based mobile applications.

## Future Enhancements

- **Log Retrieval**: Implement SD card log reading in `getLatestLogEntries()`
- **Additional Commands**: Extend command processing for device control
- **Response Mechanism**: Implement two-way command/response pattern
- **Authentication**: Add BLE pairing and authentication

## API Reference

### BootBootsBluetoothService Class

#### Methods

- `BootBootsBluetoothService()` - Constructor
- `void init(const char* deviceName)` - Initialize BLE service
- `void updateSystemStatus(const SystemState& state)` - Update and notify status
- `void setLogData(const String& logData)` - Set log data for retrieval
- `bool isConnected()` - Check if client is connected
- `void handleCommand(const String& command)` - Process command (not implemented)

#### Callbacks (Automatic)

- `onConnect(BLEServer*)` - Client connection handler
- `onDisconnect(BLEServer*)` - Client disconnection handler
- `onWrite(BLECharacteristic*)` - Characteristic write handler
- `onRead(BLECharacteristic*)` - Characteristic read handler

### Private Helper Methods

- `String formatSystemStatusJson(const SystemState&)` - Format status as JSON
- `String getLatestLogEntries(int maxEntries)` - Retrieve log entries
- `void processCommand(const String&)` - Parse and execute commands
- `void sendResponse(const String&)` - Send command response
