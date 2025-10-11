# BluetoothService

General-purpose Bluetooth Low Energy (BLE) service for monitoring and controlling the BootBoots CatCam device.

## Purpose

BluetoothService provides a comprehensive BLE interface for mobile applications to monitor system status, retrieve logs, and send commands to the CatCam device. This service is independent of firmware updates and focuses on real-time device interaction and diagnostics.

## Features

- **System Status Monitoring**: Real-time JSON status updates with notifications (configurable frequency)
- **Chunked Log Retrieval**: Efficient transfer of large log files via BLE using chunked protocol
- **Command Interface**: Send commands to control device behavior
- **Auto-Reconnection**: Automatic advertising restart on client disconnect
- **SystemState Integration**: Exposes device state including WiFi, camera, sensors, and detection statistics
- **BLE Server Sharing**: Exposes BLE server for use by other services (e.g., BluetoothOTA)

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

#### Logs Characteristic (Read/Notify)
**UUID**: `bb00b007-f1a2-49fa-89c5-31e705b74d86`

Returns log entries via chunked transfer protocol to handle large log files efficiently within BLE MTU limits.

**Chunk Messages**:
```json
{
  "type": "log_chunk",
  "chunk": 1,
  "total": 5,
  "data": "[\"log line 1\",\"log line 2\",...]"
}
```

**Completion Message**:
```json
{
  "type": "logs_complete",
  "total_chunks": 5,
  "total_bytes": 2048
}
```

**Note**: Log data is sent in 400-byte chunks to stay within BLE MTU limits (~512 bytes).

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
  "command": "get_logs",
  "entries": -1
}
```

```json
{
  "command": "ping"
}
```

**Supported Commands**:
- `get_status` - Request current system status
- `get_logs` / `request_logs` - Request log entries (specify `entries` count, use -1 for all entries)
- `ping` - Health check (responds with "pong")

**Command Responses**:
Commands are responded to via notifications on the command characteristic. Large responses (like logs) use the chunked transfer protocol.

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
    // Update status periodically to avoid saturating BLE bandwidth
    // Recommended: every 10 seconds instead of every loop iteration
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 10000) {
        btService.updateSystemStatus(systemState);
        lastStatusUpdate = millis();
    }

    delay(1000);
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
- **Status update frequency**: Recommended 10 seconds to avoid saturating BLE bandwidth
- **Chunked log transfer**: Uses 400-byte chunks with 50ms delay between chunks
- **BLE server sharing**: Exposes BLE server via `getServer()` for use by other services

## Advertising Configuration

The service configures advertising for optimal iOS compatibility:
- Scan response enabled
- Min preferred connection interval: 0x06 and 0x12 (helps with iPhone connections)

## Web Bluetooth Compatibility

All UUIDs are lowercase for compatibility with the Web Bluetooth API, enabling web-based mobile applications.

## Chunked Log Transfer Protocol

To handle large log files within BLE MTU limits (~512 bytes), logs are transferred using a chunked protocol:

### Protocol Flow

1. Client sends `get_logs` command with optional `entries` parameter (-1 for all)
2. Service retrieves logs from SD card via SDLogger
3. Service splits log data into 400-byte chunks
4. Service sends each chunk as notification with metadata:
   - `type`: "log_chunk"
   - `chunk`: Current chunk number (1-indexed)
   - `total`: Total number of chunks
   - `data`: Chunk data (up to 400 bytes)
5. Service sends completion message when done:
   - `type`: "logs_complete"
   - `total_chunks`: Number of chunks sent
   - `total_bytes`: Total bytes transferred
6. Client reassembles chunks into complete log data

### Example Sequence

```
Client → Device: {"command":"get_logs","entries":-1}
Device → Client: {"type":"log_chunk","chunk":1,"total":5,"data":"[\"line1\",\"line2\",...]"}
Device → Client: {"type":"log_chunk","chunk":2,"total":5,"data":"\"line10\",\"line11\",...]"}
...
Device → Client: {"type":"log_chunk","chunk":5,"total":5,"data":"\"line40\"]"}
Device → Client: {"type":"logs_complete","total_chunks":5,"total_bytes":2048}
```

### Performance Considerations

- 50ms delay between chunks prevents overwhelming BLE stack
- 400-byte chunk size provides safety margin below 512-byte MTU
- Status updates reduced to 10-second intervals to free bandwidth for log transfers

## BLE Server Sharing

BluetoothService creates the primary BLE server for the device. Other services (like BluetoothOTA) can share this server instead of creating their own, since ESP32 supports only ONE BLE server instance.

### Usage Example

```cpp
// In main.cpp
BootBootsBluetoothService* bluetoothService = new BootBootsBluetoothService();
BluetoothOTA* bluetoothOTA = new BluetoothOTA();

bluetoothService->init("BootBoots-CatCam");
bluetoothOTA->initWithExistingServer(bluetoothService->getServer());  // Share the server
```

### API

```cpp
BLEServer* getServer()  // Returns pointer to BLE server for sharing
```

## Future Enhancements

- **Additional Commands**: Extend command processing for device control
- **Authentication**: Add BLE pairing and authentication
- **Compression**: Add compression for large log transfers

## API Reference

### BootBootsBluetoothService Class

#### Methods

- `BootBootsBluetoothService()` - Constructor
- `void init(const char* deviceName)` - Initialize BLE service
- `void updateSystemStatus(const SystemState& state)` - Update and notify status
- `void setLogData(const String& logData)` - Set log data for retrieval
- `bool isConnected()` - Check if client is connected
- `void handleCommand(const String& command)` - Process command (not implemented)
- `BLEServer* getServer()` - Get BLE server for sharing with other services

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
