# BootBoots Firmware Build & Deployment Guide

## Quick Start

```bash
# Install Python dependencies
pip install -r requirements.txt

# Build firmware and upload to S3
python scripts/build_and_upload.py

# Build only (no upload)
python scripts/build_and_upload.py --build-only
```

---

## Prerequisites

### 1. Development Tools
- **PlatformIO** - `pip install platformio`
- **Python 3.7+** with boto3 - `pip install -r requirements.txt`
- **AWS CLI** configured with credentials

### 2. AWS Setup
- S3 bucket: `bootboots-firmware-updates`
- IAM permissions: `s3:PutObject`, `s3:GetObject`, `s3:ListBucket`

### 3. ESP32 Configuration
- Copy `include/secrets.h.template` to `include/secrets.h`
- Set WiFi credentials and OTA password

---

## Build Script Usage

### Basic Commands

```bash
# Patch version bump (1.0.0 → 1.0.1)
python scripts/build_and_upload.py

# Minor version bump (1.0.1 → 1.1.0)
python scripts/build_and_upload.py --version-type minor

# Major version bump (1.1.0 → 2.0.0)
python scripts/build_and_upload.py --version-type major

# Build without version bump
python scripts/build_and_upload.py --no-bump

# Build only (no S3 upload)
python scripts/build_and_upload.py --build-only
```

### What the Script Does

1. **Version Management**
   - Reads current version from `include/version.h`
   - Bumps version according to semantic versioning
   - Updates `version.h` with new version

2. **Build Process**
   - Cleans previous build artifacts
   - Compiles firmware using PlatformIO
   - Generates `firmware.bin` in `.pio/build/esp32cam/`

3. **S3 Upload**
   - Uploads firmware to `s3://bootboots-firmware-updates/BootBoots/{version}/firmware.bin`
   - Updates `manifest.json` with version metadata
   - Adds file size and timestamp information

---

## S3 Bucket Structure

```
bootboots-firmware-updates/
├── BootBoots/
│   ├── manifest.json
│   ├── 1.0.0/
│   │   └── firmware.bin
│   ├── 1.0.1/
│   │   └── firmware.bin
│   ├── 1.1.0/
│   │   └── firmware.bin
│   └── ...
└── [other projects]/
```

### manifest.json Format

```json
{
  "project": "BootBoots",
  "versions": [
    {
      "version": "1.1.0",
      "timestamp": "2025-10-04T14:30:00+00:00",
      "firmware_path": "BootBoots/1.1.0/firmware.bin",
      "size": 1048576
    },
    {
      "version": "1.0.1",
      "timestamp": "2025-10-03T10:15:00+00:00",
      "firmware_path": "BootBoots/1.0.1/firmware.bin",
      "size": 1045678
    }
  ]
}
```

---

## Version Management

### Semantic Versioning

- **MAJOR** (X.0.0): Breaking changes, incompatible API changes
- **MINOR** (1.X.0): New features, backward-compatible
- **PATCH** (1.0.X): Bug fixes, security updates

### Version File Structure

The `include/version.h` file is auto-generated:

```cpp
#define FIRMWARE_VERSION "1.0.1"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define PROJECT_NAME "BootBoots"

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 1

#define VERSION_STRING PROJECT_NAME " v" FIRMWARE_VERSION " (" BUILD_TIMESTAMP ")"
```

---

## Development Workflow

### 1. Make Code Changes
```bash
# Edit source files
vim src/main.cpp
```

### 2. Test Locally
```bash
# Build without uploading
python scripts/build_and_upload.py --build-only

# Flash directly via USB
pio run --target upload
```

### 3. Deploy to S3
```bash
# Build and upload with version bump
python scripts/build_and_upload.py --version-type patch
```

### 4. Deploy via Web Interface
1. Open web app at https://sandbox.nakomis.com
2. Navigate to "Firmware Manager"
3. Connect to device via Bluetooth
4. Select new version and deploy

---

## OTA Update Flow

```
┌─────────────────┐
│  Build Script   │
│  build_and_     │
│  upload.py      │
└────────┬────────┘
         │ Uploads
         ↓
┌─────────────────┐
│   S3 Bucket     │
│  bootboots-     │
│  firmware-      │
│  updates        │
└────────┬────────┘
         │ Signed URL
         ↓
┌─────────────────┐     Bluetooth      ┌─────────────────┐
│   Web App       │ ←─────────────────→ │   ESP32 Device  │
│  FirmwareManager│     OTA Command     │  BluetoothOTA   │
└─────────────────┘                     └────────┬────────┘
                                                 │ HTTP Download
                                                 ↓
                                        ┌─────────────────┐
                                        │   OTAUpdate     │
                                        │  Service        │
                                        └─────────────────┘
```

### Update Process

1. **Web App**: User selects firmware version
2. **Web App**: Generates S3 signed URL (1 hour expiration)
3. **Web App**: Sends `ota_update` command via Bluetooth with URL
4. **ESP32**: Receives command, validates URL
5. **ESP32**: Downloads firmware directly from S3 via HTTPS
6. **ESP32**: Writes to flash memory with progress updates
7. **ESP32**: Reboots with new firmware
8. **Web App**: Monitors progress via BLE notifications

---

## Troubleshooting

### Build Fails

**Problem**: `pio: command not found`
```bash
# Install PlatformIO
pip install platformio
```

**Problem**: Build errors with missing libraries
```bash
# Clean and rebuild
pio run --target clean
pio lib install
pio run
```

### Upload Fails

**Problem**: AWS credentials not configured
```bash
# Configure AWS CLI
aws configure
# Or set environment variables
export AWS_ACCESS_KEY_ID=your_key
export AWS_SECRET_ACCESS_KEY=your_secret
```

**Problem**: S3 bucket access denied
```bash
# Verify bucket exists
aws s3 ls s3://bootboots-firmware-updates

# Check IAM permissions
aws sts get-caller-identity
```

### OTA Update Fails

**Problem**: ESP32 can't connect to WiFi
- Check WiFi credentials in `secrets.h`
- Verify network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check router allows ESP32 MAC address

**Problem**: Download from S3 fails
- Verify signed URL not expired (1 hour default)
- Check ESP32 serial monitor for HTTP errors
- Ensure device has internet access

**Problem**: Flash write fails
- Check available flash space
- Verify firmware size < available partition size
- Try power cycle and retry

---

## Advanced Options

### Manual PlatformIO Build

```bash
# Build only
pio run

# Upload via USB
pio run --target upload

# Clean build
pio run --target clean

# Build specific environment
pio run -e esp32cam
```

### Direct S3 Upload (without script)

```bash
# Build firmware first
pio run

# Upload to S3
aws s3 cp .pio/build/esp32cam/firmware.bin \
  s3://bootboots-firmware-updates/BootBoots/1.0.1/firmware.bin
```

### Testing OTA Locally

```bash
# Start local HTTP server
python -m http.server 8000

# In ESP32 code, use local URL
// const char* firmware_url = "http://192.168.1.100:8000/firmware.bin";
```

---

## Security Best Practices

1. **S3 Signed URLs**
   - URLs expire after 1 hour
   - Generated on-demand per update request
   - No permanent public access to firmware

2. **OTA Password**
   - Set in `secrets.h`
   - Required for network-based OTA (ArduinoOTA)
   - Use strong, unique password

3. **HTTPS Downloads**
   - ESP32 downloads firmware via HTTPS
   - Validates SSL certificates
   - Prevents man-in-the-middle attacks

4. **Bluetooth Security**
   - BLE communication uses characteristic-based access
   - Consider adding authentication in production

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build and Deploy Firmware

on:
  push:
    tags:
      - 'v*'

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Set up Python
        uses: actions/setup-python@v2
        with:
          python-version: '3.9'

      - name: Install dependencies
        run: pip install -r requirements.txt

      - name: Build and upload firmware
        env:
          AWS_ACCESS_KEY_ID: ${{ secrets.AWS_ACCESS_KEY_ID }}
          AWS_SECRET_ACCESS_KEY: ${{ secrets.AWS_SECRET_ACCESS_KEY }}
        run: |
          cd embedded/catcam/cpp/catcam
          python scripts/build_and_upload.py --no-bump
```

---

## Monitoring & Logs

### ESP32 Serial Monitor

```bash
# Monitor with PlatformIO
pio device monitor

# Or with screen
screen /dev/cu.usbserial-* 115200
```

### Expected Log Output

```
=== BootBoots System Starting ===
[INFO] Initializing hardware...
[INFO] SD Logger initialized successfully
[INFO] WiFi connected successfully
[INFO] IP Address: 192.168.1.100
[INFO] Bluetooth Service initialized
[INFO] OTA Update service initialized
[INFO] Bluetooth OTA service initialized
[INFO] Bluetooth OTA enabled - remote updates via web interface
=== BootBoots System Ready ===
```

### S3 Access Logs

Enable S3 server access logging to track firmware downloads:

```bash
aws s3api put-bucket-logging \
  --bucket bootboots-firmware-updates \
  --bucket-logging-status file://logging.json
```

---

## Support & Resources

- **Project README**: See main project README.md
- **OTA System**: See README_OTA_SYSTEM.md in old.catcam
- **PlatformIO Docs**: https://docs.platformio.org
- **ESP32 OTA Docs**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html

---

## Quick Reference

| Command | Action |
|---------|--------|
| `python scripts/build_and_upload.py` | Build + upload with patch bump |
| `--version-type minor` | Minor version bump |
| `--version-type major` | Major version bump |
| `--no-bump` | Use current version |
| `--build-only` | Build without S3 upload |
| `pio run` | Build firmware only |
| `pio run --target upload` | Flash via USB |
| `pio device monitor` | Open serial monitor |

**S3 Bucket**: `bootboots-firmware-updates`
**Project Name**: `BootBoots`
**Current Version**: Check `include/version.h`
