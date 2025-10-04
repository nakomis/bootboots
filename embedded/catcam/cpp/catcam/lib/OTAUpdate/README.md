# OTAUpdate

Over-the-Air (OTA) firmware update service for ESP32-based BootBoots CatCam device.

## Purpose

OTAUpdate provides comprehensive firmware update capabilities for ESP32 devices, supporting both local network-based updates (ArduinoOTA) and remote HTTP/HTTPS URL-based updates. This library handles the complete update lifecycle including download, flash writing, progress tracking, and error recovery.

## Features

- **Dual Update Methods**:
  - ArduinoOTA for local network updates (WiFi)
  - HTTP/HTTPS URL-based updates (e.g., from S3 signed URLs)
- **Progress Tracking**: Real-time progress reporting (0-100%)
- **Status Monitoring**: Detailed status messages throughout update process
- **Callback System**: Custom callbacks for update events
- **Error Handling**: Comprehensive error detection and reporting
- **Security**: Optional password protection for network updates
- **Cancellation**: Ability to cancel in-progress HTTP updates
- **mDNS Discovery**: Device discovery via `hostname.local`

## Usage

### Basic Initialization

```cpp
#include <OTAUpdate.h>

OTAUpdate otaUpdate;

void setup() {
    // Connect to WiFi first
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    // Initialize OTA with hostname and optional password
    otaUpdate.init("BootBoots-CatCam", "secure_password");
}

void loop() {
    // Call in main loop to handle OTA events
    otaUpdate.handle();
}
```

### HTTP/HTTPS Update from URL

```cpp
// Set callback to handle update completion
otaUpdate.setUpdateCallback([](bool success, const char* error) {
    if (success) {
        Serial.println("Update successful!");
    } else {
        Serial.printf("Update failed: %s\n", error);
    }
});

// Start update from URL (e.g., S3 signed URL)
const char* firmwareURL = "https://example.com/firmware.bin";
bool started = otaUpdate.updateFromURL(firmwareURL);

if (started) {
    Serial.println("Update started...");

    // Monitor progress in loop
    while (otaUpdate.isUpdating()) {
        Serial.printf("Status: %s (%d%%)\n",
                     otaUpdate.getStatus().c_str(),
                     otaUpdate.getProgress());
        delay(1000);
    }
}
```

### Network-Based OTA (ArduinoOTA)

```cpp
// ArduinoOTA is automatically enabled during init()
// Update using Arduino IDE or platformio:
// platformio run -t upload --upload-port BootBoots-CatCam.local
```

### Cancel Update

```cpp
if (otaUpdate.isUpdating()) {
    otaUpdate.cancelUpdate();
}
```

### Status and Progress

```cpp
String currentStatus = otaUpdate.getStatus();
int progressPercent = otaUpdate.getProgress();
bool updating = otaUpdate.isUpdating();
```

## Update Process Flow

### HTTP/HTTPS Update

1. **Connection**: Connect to firmware URL
2. **Validation**: Verify firmware size
3. **Download**: Stream firmware in 1KB chunks
4. **Flash**: Write to ESP32 flash memory
5. **Verification**: Validate written data
6. **Reboot**: Automatic restart on success

### Progress Stages

- `"Connecting to firmware server..."` - Initial connection
- `"Starting firmware download..."` - Beginning download
- `"Downloading: X%"` - Download progress
- `"Update completed - rebooting..."` - Success
- `"Update failed: <error>"` - Failure

## Configuration

### Set Password

```cpp
// Set/update OTA password
otaUpdate.setPassword("new_password");
```

### Enable Secure Mode

```cpp
// Require password for updates
otaUpdate.enableSecureMode(true);
```

**Note**: Enabling secure mode without a password will log a warning.

## Callbacks

### Update Callback

```cpp
void otaUpdateCallback(bool success, const char* error) {
    if (success) {
        // Update successful - device will reboot
        SDLogger::getInstance().infof("OTA update successful");
    } else {
        // Update failed
        SDLogger::getInstance().errorf("OTA update failed: %s", error);
    }
}

otaUpdate.setUpdateCallback(otaUpdateCallback);
```

## Error Handling

### ArduinoOTA Errors

- `OTA_AUTH_ERROR` - Authentication failed (wrong password)
- `OTA_BEGIN_ERROR` - Failed to start update process
- `OTA_CONNECT_ERROR` - Connection failed
- `OTA_RECEIVE_ERROR` - Data reception error
- `OTA_END_ERROR` - Failed to complete update

### HTTP Update Errors

- Connection timeout (30 seconds)
- Invalid firmware size
- Write failures during flash
- Network disconnection during download

All errors are logged via SDLogger and trigger the callback with `success=false`.

## Dependencies

### Internal
- **SDLogger**: Logging functionality

### External
- `Arduino.h`
- `WiFi.h`
- `WiFiClient.h`
- `ArduinoOTA.h`
- `ESPmDNS.h`
- `Update.h` (ESP32 core)
- `HTTPClient.h`

## Network Requirements

- Active WiFi connection required
- For network OTA: Device must be on same network as update client
- For HTTP updates: Internet access or network access to firmware URL
- mDNS discovery: `.local` hostname resolution

## Security Considerations

### Password Protection

```cpp
// Enable password for network OTA
otaUpdate.init("hostname", "secure_password_here");
```

- Use strong passwords for production deployments
- Password protects ArduinoOTA (not HTTP updates)
- For HTTP updates, use signed URLs with expiration

### HTTPS Support

- HTTP client supports HTTPS
- Consider using certificate validation for production
- S3 pre-signed URLs recommended for secure firmware distribution

## Memory Considerations

- Requires enough free heap for HTTP client and buffers
- 1KB buffer for firmware streaming
- Free heap is logged at update start
- Update process reserves flash partition space

## Integration with BluetoothOTA

OTAUpdate is designed to work seamlessly with BluetoothOTA:

```cpp
#include <OTAUpdate.h>
#include <BluetoothOTA.h>

OTAUpdate otaUpdate;
BluetoothOTA bleOTA;

void setup() {
    otaUpdate.init("BootBoots-CatCam");
    bleOTA.init("BootBoots-CatCam");
    bleOTA.setOTAUpdate(&otaUpdate);  // Link services
}
```

See BluetoothOTA documentation for BLE-triggered updates.

## Implementation Notes

- Uses singleton pattern for ArduinoOTA callbacks
- Static `_instance` pointer for callback access
- Automatic reboot on successful update
- System disabled during update for safety
- Progress logged every 5 seconds (network OTA) or 10% (HTTP)
- mDNS responder commented out but available if needed

## Status Values

### Common Status Messages

- `"Not initialized"` - Service not initialized
- `"Ready for updates"` - Initialized and ready
- `"OTA update in progress - system disabled"` - Active update
- `"Updating: X%"` - Network OTA progress
- `"Downloading: X%"` - HTTP update progress
- `"Update completed - rebooting"` - Success
- `"Update failed: <reason>"` - Failure
- `"Update cancelled"` - User cancellation

## Troubleshooting

### Update Fails to Start

- Check WiFi connection
- Verify sufficient free heap memory
- Ensure no other update in progress
- Check firmware URL accessibility

### Network OTA Not Discoverable

- Verify mDNS is working (ping `hostname.local`)
- Check firewall settings
- Ensure device and client on same network
- Verify hostname is unique

### HTTP Update Fails

- Check URL accessibility
- Verify firmware file is valid
- Ensure sufficient flash space
- Check network stability during download

## API Reference

### OTAUpdate Class

#### Public Methods

- `OTAUpdate()` - Constructor
- `~OTAUpdate()` - Destructor
- `void init(const char* hostname, const char* password)` - Initialize service
- `void handle()` - Process OTA events (call in loop)
- `bool isUpdating()` - Check if update in progress
- `bool updateFromURL(const char* firmwareURL)` - Start HTTP update
- `void cancelUpdate()` - Cancel HTTP update
- `void setUpdateCallback(void (*callback)(bool, const char*))` - Set completion callback
- `void setPassword(const char* password)` - Update password
- `void enableSecureMode(bool enable)` - Enable/disable secure mode
- `String getStatus()` - Get current status message
- `int getProgress()` - Get progress percentage (0-100)

#### Private Methods

Static callbacks for ArduinoOTA:
- `static void onStart()` - Update start handler
- `static void onEnd()` - Update completion handler
- `static void onProgress(unsigned int progress, unsigned int total)` - Progress handler
- `static void onError(ota_error_t error)` - Error handler

## Version Compatibility

Requires:
- ESP32 Arduino Core
- ArduinoOTA library
- ESP32 mDNS support
- `version.h` for firmware version reporting

## License

Part of the BootBoots CatCam project.
