# BluetoothOTA

Bluetooth Low Energy (BLE) interface for triggering and monitoring over-the-air (OTA) firmware updates on the BootBoots CatCam device.

## Purpose

BluetoothOTA provides a BLE-based wrapper around the OTAUpdate service, allowing mobile applications to remotely trigger firmware updates via Bluetooth. This enables wireless firmware updates without requiring the device to be on the same WiFi network as the update server.

## Features

- **BLE Command Interface**: Accepts JSON-formatted commands via BLE characteristic writes
- **Status Notifications**: Sends real-time update progress and status to connected clients
- **Firmware URL Support**: Downloads firmware from HTTP/HTTPS URLs (e.g., S3 signed URLs)
- **Update Management**: Start, cancel, and monitor firmware updates
- **Connection Handling**: Automatic reconnection and advertising management

## BLE Service Specification

### Service UUID
```
12345678-1234-1234-1234-123456789abc
```

### Characteristics

#### Command Characteristic (Write)
**UUID**: `87654321-4321-4321-4321-cba987654321`

Accepts JSON commands:
```json
{
  "action": "ota_update",
  "firmware_url": "https://example.com/firmware.bin",
  "version": "1.2.3"
}
```

**Supported Actions**:
- `ota_update` - Start firmware update from URL
- `get_status` - Request current update status
- `cancel_update` - Cancel in-progress update

#### Status Characteristic (Read/Notify)
**UUID**: `11111111-2222-3333-4444-555555555555`

Returns JSON status:
```json
{
  "status": "downloading",
  "message": "Downloading firmware...",
  "progress": 45,
  "version": "1.0.0"
}
```

**Status Values**:
- `ready` - Service initialized and ready
- `connected` - Client connected
- `starting` - Update initialization
- `downloading` - Firmware download in progress
- `error` - Update failed
- `cancelled` - Update cancelled by user

## Usage

### Initialization

```cpp
#include <BluetoothOTA.h>
#include <OTAUpdate.h>

BluetoothOTA bleOTA;
OTAUpdate otaUpdate;

void setup() {
    // Initialize OTA update service first
    otaUpdate.init("BootBoots-CatCam");

    // Initialize Bluetooth OTA and link to OTA service
    bleOTA.init("BootBoots-CatCam");
    bleOTA.setOTAUpdate(&otaUpdate);
}

void loop() {
    bleOTA.handle();
    otaUpdate.handle();
}
```

### Check Connection Status

```cpp
if (bleOTA.isConnected()) {
    // Client is connected
}
```

### Send Status Updates

```cpp
bleOTA.sendStatusUpdate("downloading", "Firmware download at 50%", 50);
```

## Dependencies

### Internal
- **OTAUpdate**: Core firmware update implementation (required)
- **SDLogger**: Logging functionality

### External
- ESP32 BLE Arduino library (`BLEDevice.h`, `BLEServer.h`, `BLEUtils.h`)
- ArduinoJson (for command/status parsing)

## Relationship to Other Components

```
Mobile App (BLE Client)
        ↓
  BluetoothOTA ──→ OTAUpdate ──→ ESP32 Update Module
        ↓                              ↓
  Status Updates                  Firmware Flash
```

**BluetoothOTA** acts as a bridge between BLE clients and the **OTAUpdate** service. It does not perform firmware updates directly; instead, it delegates to OTAUpdate and relays status information back to the client.

## Implementation Notes

- Device name defaults to "BootBoots-CatCam"
- Automatically restarts advertising when client disconnects
- Requires WiFi connection for HTTP firmware downloads
- OTAUpdate must be initialized before BluetoothOTA can function
- All status messages are logged via SDLogger

## Security Considerations

- No authentication required for BLE connection (consider adding in production)
- Firmware URLs should use HTTPS with signed URLs for security
- OTAUpdate supports password protection for network-based OTA

## API Reference

### BluetoothOTA Class

#### Methods

- `bool init(const char* deviceName)` - Initialize BLE service
- `void handle()` - Process BLE events (call in loop)
- `bool isConnected()` - Check if client is connected
- `void sendStatusUpdate(const String& status, const String& message, int progress)` - Send status to client
- `void handleOTACommand(const String& commandJson)` - Process OTA command
- `void setOTAUpdate(OTAUpdate* otaUpdate)` - Link to OTAUpdate instance

### Callbacks

Internal server and characteristic callbacks are handled automatically.
