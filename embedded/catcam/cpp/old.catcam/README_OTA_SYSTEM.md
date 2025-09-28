# BootBoots S3-Based OTA Firmware Management System

## Overview

This system provides a complete Over-The-Air (OTA) firmware update solution for BootBoots ESP32 devices using S3 storage, web-based management, and Bluetooth communication.

## Architecture

```
Web App (React) ←→ Bluetooth ←→ ESP32 Device
     ↓                              ↓
  S3 Bucket ←→ Signed URLs ←→ HTTP Download
```

### Components

1. **ESP32 Firmware Components**
   - `version.h` - Version management
   - `OTAUpdate` - HTTP OTA downloads from S3
   - `BluetoothOTA` - Bluetooth command interface
   - `build_and_upload.py` - Automated build and S3 upload

2. **Web Application Components**
   - `FirmwareManager.tsx` - React UI for firmware management
   - `firmwareService.ts` - S3 integration and signed URLs
   - `bluetoothService.ts` - Web Bluetooth API communication

3. **S3 Storage Structure**
   ```
   mhsometestbucket/
   ├── BootBoots/
   │   ├── manifest.json
   │   ├── 1.0.0/
   │   │   └── firmware.bin
   │   ├── 1.1.0/
   │   │   └── firmware.bin
   │   └── ...
   └── MushroomHumidor/
       └── ...
   ```

## Setup Instructions

### 1. ESP32 Firmware Setup

1. **Install Dependencies**
   ```bash
   cd /path/to/bootboots/embedded/catcam/cpp/catcam
   pip install -r requirements.txt
   ```

2. **Configure Secrets**
   - Copy `include/secrets.h.template` to `include/secrets.h`
   - Set your WiFi credentials and OTA password

3. **Build and Upload Initial Firmware**
   ```bash
   # Build and upload to S3 (bumps version automatically)
   python scripts/build_and_upload.py
   
   # Build only (no S3 upload)
   python scripts/build_and_upload.py --build-only
   
   # Bump major/minor version
   python scripts/build_and_upload.py --version-type major
   ```

### 2. Web Application Setup

1. **Install Dependencies**
   ```bash
   cd /path/to/sandbox-app
   npm install
   ```

2. **Configure AWS Credentials**
   - Ensure your AWS credentials are configured for S3 access
   - Update S3 bucket name in `firmwareService.ts` if needed

3. **Start Development Server**
   ```bash
   npm start
   ```

### 3. S3 Bucket Setup

1. **Create S3 Bucket**
   ```bash
   aws s3 mb s3://mhsometestbucket
   ```

2. **Configure CORS** (for direct web access)
   ```json
   [
     {
       "AllowedHeaders": ["*"],
       "AllowedMethods": ["GET", "HEAD"],
       "AllowedOrigins": ["*"],
       "ExposeHeaders": []
     }
   ]
   ```

## Usage Workflow

### 1. Building and Deploying Firmware

```bash
# Automatic version bump and S3 upload
python scripts/build_and_upload.py

# Manual version specification
python scripts/build_and_upload.py --version-type minor
```

This will:
- Bump the version number in `version.h`
- Build the firmware using PlatformIO
- Upload `firmware.bin` to S3 at `BootBoots/{version}/firmware.bin`
- Update the `manifest.json` with the new version

### 2. Web-Based OTA Updates

1. **Open Web Application**
   - Navigate to the "Firmware Manager" tab
   - The system will automatically load available firmware versions from S3

2. **Connect to Device**
   - Click "Connect to BootBoots Device"
   - Select your ESP32 device from the Bluetooth dialog

3. **Deploy Firmware**
   - Select the desired firmware version
   - Click "Deploy Firmware Update"
   - Monitor progress in real-time

### 3. Update Process Flow

1. **Web App** → Generates signed S3 URL for selected firmware
2. **Web App** → Sends OTA command via Bluetooth to ESP32
3. **ESP32** → Receives signed URL and starts HTTP download
4. **ESP32** → Downloads firmware directly from S3
5. **ESP32** → Installs firmware and reboots
6. **ESP32** → Reports status back via Bluetooth

## API Reference

### Build Script Options

```bash
python scripts/build_and_upload.py [OPTIONS]

Options:
  --version-type {major,minor,patch}  Version bump type (default: patch)
  --no-bump                          Skip version bump
  --build-only                       Build without uploading to S3
```

### Bluetooth OTA Commands

The ESP32 accepts JSON commands via Bluetooth:

```json
{
  "action": "ota_update",
  "firmware_url": "https://signed-s3-url...",
  "version": "1.1.0"
}
```

```json
{
  "action": "get_status"
}
```

```json
{
  "action": "cancel_update"
}
```

### Status Responses

```json
{
  "status": "downloading",
  "message": "Downloading firmware: 45%",
  "progress": 45,
  "version": "1.0.0"
}
```

## Security Features

- **Password-Protected OTA**: ESP32 OTA updates require authentication
- **Signed URLs**: S3 firmware downloads use temporary signed URLs
- **Bluetooth Security**: BLE communication with characteristic-based access control
- **Version Verification**: Firmware versions are tracked and verified

## Troubleshooting

### Common Issues

1. **Build Script Fails**
   - Ensure PlatformIO is installed: `pip install platformio`
   - Check AWS credentials: `aws configure list`

2. **Bluetooth Connection Fails**
   - Ensure device is advertising (check serial logs)
   - Use Chrome/Edge browser (Web Bluetooth support required)
   - Check that ESP32 is within Bluetooth range

3. **S3 Upload Fails**
   - Verify AWS credentials and S3 permissions
   - Check bucket exists: `aws s3 ls s3://mhsometestbucket`

4. **Firmware Download Fails**
   - Check WiFi connectivity on ESP32
   - Verify signed URL is valid (check expiration)
   - Monitor ESP32 serial output for HTTP errors

### Debug Logs

**ESP32 Serial Output:**
```
[INFO] Bluetooth OTA service initialized successfully
[INFO] Received OTA command: {"action":"ota_update",...}
[INFO] Starting HTTP OTA update from: https://...
[INFO] Download progress: 50% (512000/1024000 bytes)
```

**Web Browser Console:**
```javascript
// Enable debug logging
localStorage.setItem('debug', 'firmware:*');
```

## File Structure

```
bootboots/embedded/catcam/cpp/catcam/
├── include/
│   ├── version.h              # Version management
│   └── secrets.h              # WiFi/OTA credentials
├── lib/
│   ├── OTAUpdate/             # HTTP OTA implementation
│   └── BluetoothOTA/          # Bluetooth OTA service
├── scripts/
│   └── build_and_upload.py    # Build automation
├── src/
│   └── main.cpp               # Main application
└── requirements.txt           # Python dependencies

sandbox-app/src/
├── components/pages/
│   ├── FirmwareManager.tsx    # React UI component
│   └── FirmwareManager.css    # Styling
└── services/
    ├── firmwareService.ts     # S3 integration
    └── bluetoothService.ts    # Bluetooth communication
```

## Version History

- **v1.0.0**: Initial S3-based OTA system
- **v1.1.0**: Added Bluetooth control interface
- **v1.2.0**: Enhanced progress monitoring and error handling

## Contributing

When adding new features:

1. Update version in `version.h`
2. Test with `--build-only` flag first
3. Update this README with new functionality
4. Deploy to S3 using the build script

## License

This OTA system is part of the BootBoots project. See main project LICENSE for details.
