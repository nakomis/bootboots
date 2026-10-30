# BootBoots ESP32 CatCam Firmware

ESP32-based camera system with bootloader-based OTA updates for maximised flash space.

## Architecture

This project uses a **bootloader-based OTA architecture** that maximises available flash space:

### ESP32-S3 (16MB Flash - Primary Target)
- **Factory Partition** (1MB @ 0x10000): Minimal bootloader that checks for pending OTA updates
- **OTA0 Partition** (7MB @ 0x110000): Main application partition
- **SPIFFS** (~8MB): File system for images and logs
- **SD Card**: Staging area for firmware downloads

### Legacy ESP32-CAM (4MB Flash)
- **Factory Partition** (448KB @ 0x10000): Minimal bootloader
- **OTA0 Partition** (3.56MB @ 0x80000): Main application partition
- **SPIFFS** (40KB): File system for logs

### Boot Sequence

```
Power On / Reboot
    |
    v
ESP32 ROM Bootloader
    |
    v
Factory Bootloader (runs every time)
    |
    v
Check NVS for pending OTA
    |
    v
  Pending? --> Flash from SD --> OTA0 --> Reboot
  No --> Boot directly into OTA0
    |
    v
CatCam App (OTA0)
    |
    v
Set boot partition to factory (for next reboot)
    |
    v
Run normally
```

## Project Structure

```
embedded/
|-- bootloader/              # Factory bootloader
|   |-- src/main.cpp         # Bootloader logic
|   |-- partitions_bootloader.csv      # 4MB partition table
|   |-- partitions_bootloader_s3.csv   # 16MB partition table
|   +-- platformio.ini       # Build configuration
|
|-- catcam/                  # Main application
|   |-- src/main.cpp         # Main application entry
|   |-- include/             # Header files
|   |-- lib/                 # Custom libraries (15+)
|   |   |-- AWSAuth/         # AWS IoT authentication
|   |   |-- BluetoothOTA/    # BLE-based OTA control
|   |   |-- BluetoothService/# Device status monitoring
|   |   |-- Camera/          # Image capture
|   |   |-- CaptureController/# Capture orchestration
|   |   |-- CatCamHttpClient/# HTTPS to SageMaker
|   |   |-- ImageStorage/    # Image file management
|   |   |-- InputManager/    # Button handling
|   |   |-- LedController/   # RGB LED control
|   |   |-- NeoPixel/        # WS2812 driver
|   |   |-- OTAUpdate/       # Two-stage OTA
|   |   |-- PCF8574Manager/  # I2C GPIO expander
|   |   |-- SDLogger/        # Thread-safe logging
|   |   |-- SystemManager/   # System state management
|   |   |-- VideoRecorder/   # MJPEG recording
|   |   +-- WifiConnect/     # WiFi management
|   |-- scripts/
|   |   |-- provision.sh         # Write a device's prov partition (Thing, Wi-Fi, certs)
|   |   |-- image-checks.sh      # What a published image must pass
|   |   |-- publish-firmware.sh  # Check + upload to S3 (never replaces a version)
|   |   +-- factory_setup.sh     # Factory setup script
|   |-- docs/provisioning.md     # prov partition, scripts, first wired flash
|   |-- partitions_custom.csv      # 4MB partition table
|   |-- partitions_custom_s3.csv   # 16MB partition table
|   +-- platformio.ini       # Build configuration
|
|-- CLAUDE.md                # Detailed technical context
+-- README.md                # This file
```

## Setup for Factory-New ESP32

### Prerequisites

- Python 3.8+
- PlatformIO CLI
- AWS CLI configured (for S3 uploads)

### Provisioning (no secrets in the image)

The firmware no longer compiles in any secrets or per-environment settings (BOOT-10; there is no
`secrets.h`). Each device reads its Thing name, Wi-Fi credentials, IoT and credentials-provider
endpoints, API host, root CA, certificate and private key from its `prov` partition (0x810000,
64 KB), which `scripts/provision.sh` writes over USB:

```bash
cd catcam
./scripts/provision.sh BootBootsThing /dev/cu.usbmodemXXXX sandbox
```

It reads `/Nakomis/Wifi/SSID`, `/Nakomis/Wifi/Password`, `/BootsBoots/<thing>/certPem` and
`/BootsBoots/<thing>/privKey` from SSM and the endpoints from IoT. Without a prov partition the
device runs with BLE only and logs `NOT PROVISIONED`. See
[catcam/docs/provisioning.md](catcam/docs/provisioning.md).

### Quick Setup

Use the automated setup script:

```bash
cd catcam
./scripts/factory_setup.sh /dev/cu.usbserial-XXXX
```

This script will:
1. Build the factory bootloader
2. Build the catcam application
3. Flash bootloader to factory partition
4. Flash catcam app to OTA0 partition
5. Set otadata to point to factory partition

Then provision it (`scripts/provision.sh <thing> <port> [env]`).

### Manual Setup

If you prefer manual setup:

#### Step 1: Build Both Projects

```bash
# Build bootloader (from embedded directory)
cd bootloader
pio run -e esp32s3cam

# Build catcam app
cd ../catcam
pio run -e esp32s3cam
```

#### Step 2: Flash Both Partitions (ESP32-S3)

```bash
# Put device in download mode (hold BOOT, plug USB into OTG port, release BOOT)
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 \
  --port /dev/cu.usbmodem* \
  --baud 921600 \
  write_flash -z --flash_mode dout --flash_freq 80m --flash_size 16MB \
  0x0 ../bootloader/.pio/build/esp32s3cam/bootloader.bin \
  0x8000 ../bootloader/.pio/build/esp32s3cam/partitions.bin \
  0x10000 ../bootloader/.pio/build/esp32s3cam/firmware.bin \
  0x110000 .pio/build/esp32s3cam/firmware.bin

# Then write its prov partition (0x810000)
./scripts/provision.sh BootBootsThing /dev/cu.usbmodemXXXX sandbox
```

#### Step 3: Verify Boot Sequence

```bash
pio device monitor -b 115200
```

You should see:
1. `[BOOTLOADER]` messages from factory partition
2. `[CATCAM] Set boot partition to factory for next reboot`
3. Device running normally

## OTA Updates

### Via Web Interface (Recommended)

1. Build, check and publish new firmware (a published version is never replaced):
   ```bash
   cd catcam
   FIRMWARE_VERSION=1.0.333 pio run -e esp32s3cam
   ./scripts/publish-firmware.sh sandbox .pio/build/esp32s3cam/firmware.bin 1.0.333
   ```

2. Open web interface at http://localhost:3000/firmware

3. Connect to device via Bluetooth

4. Select new version and click "Update"

5. Monitor progress in web UI

### OTA Update Flow

```
Web UI --> Download firmware from S3 to SD card
    |
    v
Set NVS flag (pending=true, size=<bytes>)
    |
    v
Reboot device
    |
    v
Factory bootloader detects pending OTA
    |
    v
Flash firmware: SD card --> OTA0 partition
    |
    v
Delete firmware file from SD card
    |
    v
Boot into new firmware
```

## Development

### Build Commands

**Standard build (ESP32-S3)**:
```bash
cd catcam
pio run -e esp32s3cam
```

**Build and upload (USB)**:
```bash
pio run -e esp32s3cam --target upload --upload-port /dev/cu.usbmodem*
```

**Check and publish to S3** (`--dry-run` to see what it would do):
```bash
./scripts/image-checks.sh .pio/build/esp32s3cam/firmware.bin 1.0.333
./scripts/publish-firmware.sh sandbox .pio/build/esp32s3cam/firmware.bin 1.0.333
```

### Firmware Version

The version is set at build time from the `FIRMWARE_VERSION` environment variable (default
`0.0.0-dev`); `include/version.h` is no longer bumped by hand. Published versions continue from
1.0.333 (1.0.331 was the last hand-published one; 1.0.332 was committed but never published).

```bash
FIRMWARE_VERSION=1.0.333 pio run -e esp32s3cam
```

### Serial Monitoring

```bash
pio device monitor -b 115200
```

### Debugging

**View logs from SD card**:
```bash
# Mount SD card and view recent logs
ls -lt /Volumes/FAT32_Mount/logs/ | head -10
cat /Volumes/FAT32_Mount/logs/<latest_log_file>
```

**Check for pending OTA**:
```bash
# Look for firmware file on SD card
ls -lh /Volumes/FAT32_Mount/firmware_update.bin
```

## Partition Tables

### ESP32-S3 (16MB Flash)

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| nvs | data/nvs | 0x9000 | 20KB | Settings storage |
| otadata | data/ota | 0xe000 | 8KB | Boot partition selector |
| **factory** | app/factory | **0x10000** | **1MB** | **Bootloader** |
| **ota_0** | app/ota_0 | **0x110000** | **7MB** | **Main app** |
| spiffs | data/spiffs | 0x810000 | ~8MB | File system |
| coredump | data/coredump | 0xFF0000 | 64KB | Crash dumps |

**Total Flash**: 16MB (ESP32-S3 N16R8)

### Legacy ESP32-CAM (4MB Flash)

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| nvs | data/nvs | 0x9000 | 20KB | Settings storage |
| otadata | data/ota | 0xe000 | 8KB | Boot partition selector |
| **factory** | app/factory | **0x10000** | **448KB** | **Bootloader** |
| **ota_0** | app/ota_0 | **0x80000** | **3.56MB** | **Main app** |
| spiffs | data/spiffs | 0x3F0000 | 40KB | File system |
| coredump | data/coredump | 0x3FA000 | 24KB | Crash dumps |

**Total Flash**: 4MB (ESP32-CAM)

## Troubleshooting

### Device stuck in boot loop

**Symptom**: Device keeps rebooting, showing bootloader messages

**Cause**: Corrupted OTA0 partition or NVS flag stuck

**Fix (ESP32-S3)**:
```bash
# Erase OTA0 partition
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port /dev/cu.usbmodem* \
  erase_region 0x110000 0x700000

# Re-flash catcam app
pio run -e esp32s3cam --target upload --upload-port /dev/cu.usbmodem*
```

### Bootloader not running

**Symptom**: Device boots directly into catcam app, no `[BOOTLOADER]` messages

**Cause**: Missing or corrupted bootloader, or otadata not set correctly

**Fix**: Run factory setup script again

### OTA update fails

**Symptom**: Download completes but device doesn't update

**Check**:
1. Verify firmware file on SD card: `ls /Volumes/FAT32_Mount/firmware_update.bin`
2. Check NVS flags via serial monitor
3. Review SD card logs for error messages

## Technical Details

### Why Bootloader-Based OTA?

Traditional ESP32 OTA requires **two app partitions** of equal size to ping-pong between updates. With 16MB flash:
- Dual partition: 2 x 7MB = 14MB used for redundancy
- Single partition + bootloader: 1MB + 7MB = 8MB, leaving more space for SPIFFS

**Result**: Maximum application size with room for large file storage.

### Boot Partition Management

The catcam app calls `esp_ota_set_boot_partition(factory)` on every boot to ensure the factory bootloader runs first. This solves the chicken-and-egg problem where the ESP32 ROM bootloader would otherwise boot directly into OTA0.

### PCF8574 I/O Expander Pin Map

The PCF8574 uses quasi-bidirectional I/O. Input pins must be written HIGH (weak pull-up) to be readable. Active-low output pins are written HIGH when inactive.

| Pin | Name | Direction | Active | Initial State |
|-----|------|-----------|--------|---------------|
| P0 | PIR Sensor | Input | HIGH | 1 (HIGH) |
| P1 | Light Sensor | Input | - | 1 (HIGH) |
| P2 | Button | Input | - | 1 (HIGH) |
| P3 | Atomizer | Output | LOW | 1 (HIGH = inactive) |
| P4 | Pressure Sensor | Input | - | 1 (HIGH) |
| P5 | LED Strip 1 | Output | LOW | 1 (HIGH = inactive) |
| P6 | LED Strip 2 | Output | LOW | 1 (HIGH = inactive) |
| P7 | Flash LED | Output | HIGH | 0 (LOW = off) |

`PCF8574_INITIAL_PIN_STATE = 0x7F`

### Memory Usage

- **Bootloader**: Minimal dependencies, ~200KB+ free heap
- **CatCam App**: BLE + WiFi + HTTP active, ~38KB free heap
- **OTA Download**: Happens in catcam app with BLE active
- **OTA Flash**: Happens in bootloader with maximum available heap

## License

CC0 1.0 Universal - Public domain dedication.
