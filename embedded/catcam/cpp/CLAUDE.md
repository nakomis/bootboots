# Claude Code Context - BootBoots CatCam

## Project Overview

This project contains firmware for the BootBoots CatCam system:
- **bootloader/** - Arduino-based factory bootloader (checks SD card for OTA updates)
- **catcam/** - Arduino-based main application (camera, WiFi, BLE, SD logging)

Both use the **Arduino framework** for simplicity and library compatibility.

## Related Projects

- **Web App + Infra**: `/Users/martinmu_1/repos/nakomis/sandboxsite` - React web interface and AWS CDK infrastructure (see CLAUDE.md in that repo)
  - `sandbox-app/` - React frontend for OTA updates and device control via Web Bluetooth
  - `infra/` - AWS CDK stacks (CloudFront, Cognito, S3)
- **S3 Bucket**: `bootboots-firmware-updates` - Stores firmware binaries and manifest (Lambda auto-updates manifest on upload)

**Note:** When adding new API Gateway endpoints that the web app calls, ensure the URL pattern is excluded from cache-first in `sandbox-app/public/sw.js` to avoid stale data.

## Supported Boards

### ESP32-S3 WROOM N16R8 CAM with OV5640 (Primary Target)

**Full Name:** ESP32-S3 WROOM N16R8 CAM Development Board WiFi + Bluetooth Module OV5640 Camera

| Feature | Specification |
|---------|---------------|
| **Chipset** | ESP32-S3 (dual-core 32-bit Xtensa LX7 @ 240MHz) |
| **Flash Memory** | 16MB (QSPI) |
| **PSRAM** | 8MB (Octal SPI) |
| **Wireless** | Wi-Fi 802.11 b/g/n (2.4 GHz), Bluetooth 5 (LE) |
| **Camera** | OV5640 (5MP, autofocus capable) |
| **USB** | Dual USB-C: UART (serial monitor) + OTG (flashing/native USB) |
| **Storage** | microSD card slot (SDMMC) |
| **LED** | WS2812 RGB addressable LED (GPIO 48) |
| **Operating Voltage** | 3.3V logic, 5V input via USB-C |
| **AI Capabilities** | Vector instructions for neural network processing |
| **Build environment** | `esp32s3cam` |

**Note:** GPIO35, GPIO36, GPIO37 are reserved for Octal PSRAM - do not use for other purposes.

### Original ESP32-CAM AI-Thinker (Legacy)
- 4MB Flash, 4MB PSRAM
- OV2640 camera (2MP)
- Single micro-USB port
- Build environment: `esp32cam`

## Partition Layout

### ESP32-S3 (16MB Flash)
```
Offset      Size       Name        Description
0x0         -          bootloader  ESP32 2nd stage bootloader
0x8000      0x1000     partitions  Partition table
0x9000      0x5000     nvs         Non-volatile storage
0xe000      0x2000     otadata     OTA data
0x10000     0x100000   factory     Arduino bootloader (1MB)
0x110000    0x700000   ota_0       Main application (7MB)
0x810000    0x7E0000   spiffs      File storage
0xFF0000    0x10000    coredump    Core dump storage
```

### Original ESP32-CAM (4MB Flash)
```
Offset      Size       Name
0x10000     0x70000    factory     (448KB)
0x80000     0x370000   ota_0       (3.5MB)
```

## Boot Flow

1. ESP32 ROM bootloader loads 2nd stage bootloader
2. 2nd stage bootloader loads **factory partition** (Arduino bootloader)
3. Arduino bootloader checks NVS for pending OTA flag
4. If pending OTA: reads firmware from SD card `/firmware_update.bin`, flashes to ota_0
5. Sets ota_0 as boot partition and reboots
6. Main app runs from **ota_0 partition**

## Pin Configurations

### ESP32-S3 CAM Board

#### Peripherals
```cpp
// SD Card (SDMMC 1-bit mode)
SD_MMC_CLK  = GPIO 39
SD_MMC_CMD  = GPIO 38
SD_MMC_D0   = GPIO 40

// UART0 (directly on UART USB-C port)
UART_TX     = GPIO 43
UART_RX     = GPIO 44

// I2C (directly accessible, no UART conflicts)
I2C_SDA     = GPIO 4
I2C_SCL     = GPIO 5

// RGB LED (WS2812 addressable NeoPixel)
RGB_LED_PIN = GPIO 48
```

#### RGB LED (WS2812) Usage

The onboard LED is a WS2812 addressable RGB LED, not a simple GPIO LED. Use the Adafruit NeoPixel or FastLED library:

```cpp
#include <Adafruit_NeoPixel.h>

#define RGB_LED_PIN 48
#define NUM_LEDS 1

Adafruit_NeoPixel pixel(NUM_LEDS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pixel.begin();
  pixel.setBrightness(50);  // 0-255
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

// Examples:
// setColor(255, 0, 0);    // Red
// setColor(0, 255, 0);    // Green
// setColor(0, 0, 255);    // Blue
// setColor(0, 0, 0);      // Off
```

#### OV5640 Camera Pinout
```cpp
// Camera control
CAM_PWDN    = -1 (not used)
CAM_RESET   = -1 (not used)
CAM_XCLK    = GPIO 15

// Camera I2C (SCCB) - shared with board I2C
CAM_SIOD    = GPIO 4  (SDA)
CAM_SIOC    = GPIO 5  (SCL)

// Camera data bus (Y2-Y9 = D0-D7)
CAM_Y2      = GPIO 11
CAM_Y3      = GPIO 9
CAM_Y4      = GPIO 8
CAM_Y5      = GPIO 10
CAM_Y6      = GPIO 12
CAM_Y7      = GPIO 18
CAM_Y8      = GPIO 17
CAM_Y9      = GPIO 16

// Camera sync
CAM_VSYNC   = GPIO 6
CAM_HREF    = GPIO 7
CAM_PCLK    = GPIO 13
```

### Original ESP32-CAM (AI-Thinker)
```cpp
// SD Card (SDMMC 4-bit mode, conflicts with some GPIOs)
// I2C shares UART0 pins (GPIO 1/3) - requires disabling serial

// LED (simple GPIO, active LOW)
LED_PIN     = GPIO 33
```

## Critical Build Settings for ESP32-S3

These settings are required in `platformio.ini` for ESP32-S3:

```ini
board = esp32s3camlcd
board_build.flash_mode = dout
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.arduino.memory_type = qio_opi
build_flags =
    -DESP32S3_CAM
    -DARDUINO_USB_CDC_ON_BOOT=0
```

**Why these matter:**
- `flash_mode = dout` - The N16R8 module uses DOUT, not QIO/DIO
- `memory_type = qio_opi` - Quad SPI flash + Octal SPI PSRAM (not opi_opi!)
- `ARDUINO_USB_CDC_ON_BOOT=0` - Use UART for serial, not USB CDC

## Build Commands

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/cpp

# Build bootloader
cd bootloader && pio run -e esp32s3cam

# Build main app
cd ../catcam && pio run -e esp32s3cam
```

## Deployment Methods

### OTA Deployment (Preferred Method)

**This is the preferred way to deploy firmware.** No USB connection required - the device is updated wirelessly via Bluetooth OTA from the web interface.

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/cpp/catcam

# Set AWS credentials for S3 upload
export AWS_PROFILE=nakom.is-sandbox

# Build, bump version, and upload to S3
python3 scripts/build_and_upload.py
```

This script:
1. Bumps the version number in `include/version.h`
2. Builds the firmware for ESP32-S3
3. Uploads to S3 bucket `bootboots-firmware-updates`
4. Lambda automatically updates the manifest

Then use the web interface at localhost:3000 to trigger the OTA update via Bluetooth.

### USB Flash Commands (Backup Method)

**Use this only when OTA is not available** (e.g., device is bricked, bootloader needs updating, or first-time flash).

#### Full Flash (both bootloader and catcam)

1. **Close the serial monitor first** (if running) - it locks the serial port
2. Put device in download mode: Hold BOOT button, plug USB into **OTG port**, release BOOT
3. Run:

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/embedded/catcam/cpp

~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port /dev/cu.usbmodem* --baud 921600 \
  write_flash -z --flash_mode dout --flash_freq 80m --flash_size 16MB \
  0x0 bootloader/.pio/build/esp32s3cam/bootloader.bin \
  0x8000 bootloader/.pio/build/esp32s3cam/partitions.bin \
  0x10000 bootloader/.pio/build/esp32s3cam/firmware.bin \
  0x110000 catcam/.pio/build/esp32s3cam/firmware.bin
```

#### Flash Only Main App (catcam)

**Remember:** Close the serial monitor first, then put device in download mode.

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port /dev/cu.usbmodem* --baud 921600 \
  write_flash -z --flash_mode dout --flash_freq 80m --flash_size 16MB \
  0x110000 catcam/.pio/build/esp32s3cam/firmware.bin
```

#### Serial Monitor (for debugging)

For debugging via USB, unplug from OTG port and plug into **UART port**:

```bash
pio device monitor -b 115200
```

## Common Issues & Solutions

### Boot Loop with "Octal Flash option selected, but EFUSE not configured"
**Cause:** Wrong `memory_type` setting (opi_opi instead of qio_opi)
**Fix:** Ensure `board_build.arduino.memory_type = qio_opi` in platformio.ini

### No Serial Output on ESP32-S3
**Cause:** Serial defaults to USB CDC on S3, but we're using UART port
**Fix:** Use `HardwareSerial DebugSerial(0)` and initialize with TX/RX pins:
```cpp
DebugSerial.begin(115200, SERIAL_8N1, 44, 43);  // RX=44, TX=43
```

### SD Card Fails on ESP32-S3
**Cause:** Default SDMMC pins don't match S3 CAM board
**Fix:** Call `SD_MMC.setPins(39, 38, 40)` before `SD_MMC.begin()`

## Project Structure

```
cpp/
├── bootloader/
│   ├── platformio.ini
│   ├── partitions_bootloader.csv      # 4MB partition table
│   ├── partitions_bootloader_s3.csv   # 16MB partition table
│   ├── include/
│   │   └── version.h
│   └── src/
│       └── main.cpp
│
├── catcam/
│   ├── platformio.ini
│   ├── partitions_custom.csv          # 4MB partition table
│   ├── partitions_custom_s3.csv       # 16MB partition table
│   ├── include/
│   │   ├── main.h
│   │   ├── secrets.h                  # WiFi credentials (not in git)
│   │   ├── SystemState.h
│   │   └── version.h
│   ├── src/
│   │   └── main.cpp
│   └── lib/
│       ├── BluetoothOTA/
│       ├── BluetoothService/
│       ├── Camera/
│       ├── OTAUpdate/
│       ├── PCF8574Manager/
│       ├── SDLogger/
│       ├── VideoRecorder/
│       └── WifiConnect/
│
└── CLAUDE.md                          # This file
```

## Secrets

WiFi credentials are in `catcam/include/secrets.h` (gitignored):
```cpp
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "your_password"
```

## Bluetooth Image Handling Architecture

### Device Side (BluetoothService)

**File:** `catcam/lib/BluetoothService/src/BluetoothService.cpp`

The device stores images in `/images/` directory with timestamp-based filenames:
- `2026-01-18T11_28_33.179Z.jpg` - JPEG image from camera
- `2026-01-18T11_28_33.179Z.txt` - AI inference result (JSON)

**Supported BLE Commands:**

| Command | Request | Response |
|---------|---------|----------|
| `list_images` | `{"command": "list_images"}` | Chunked: `image_list_chunk` → `image_list_complete` |
| `get_image` | `{"command": "get_image", "filename": "...jpg"}` | Chunked: `image_start` → `image_chunk` (base64) → `image_complete` |
| `get_image_metadata` | `{"command": "get_image_metadata", "filename": "...jpg"}` | `metadata_result` with `.txt` contents |
| `take_photo` | `{"command": "take_photo"}` | `photo_started` → (capture + AI inference) → `photo_complete` with new filename |

**Chunking Protocol:**
- BLE MTU is ~512 bytes, so data is sent in chunks
- Image chunks: 300 bytes raw → ~400 bytes base64 per notification
- 30ms delay between chunks to avoid overwhelming BLE stack
- LED toggles green/blue during transfer for visual feedback

### Web App Side (Bluetooth.tsx)

**File:** `/Users/martinmu_1/repos/nakomis/sandboxsite/sandbox-app/src/components/pages/Bluetooth.tsx`

**State Management:**
- `imageList: string[]` - Available image filenames from device
- `selectedImage: string` - Currently selected filename
- `currentImage: string | null` - Base64 data URL of displayed image
- `currentMetadata: string | null` - AI inference result from `.txt` file
- `imageCache: LRUCache<string, ImageAndResult>` - Caches up to 20 images with metadata

**ImageAndResult Type:**
```typescript
interface ImageAndResult {
    imageData: string;      // base64 data URL
    metadata: string | null; // .txt file contents (AI inference JSON)
}
```

**Data Flow:**
1. User clicks "Get Images" → `list_images` command → dropdown populated
2. User selects image → check cache first, then `get_image` + `get_image_metadata` commands
3. User clicks "Take Photo" → `take_photo` command → auto-refresh list and select new image

### AI Inference Result Format (.txt files)

```json
{
    "success": true,
    "data": {
        "probabilities": [0.05, 0.10, 0.15, 0.20, 0.40, 0.10]
    },
    "mostLikelyCat": {
        "name": "Tau",
        "confidence": 0.40,
        "index": 4
    }
}
```

### Image Cleanup

Device maintains max 20 image pairs (`.jpg` + `.txt`). When limit exceeded, oldest pairs are deleted automatically during photo capture.

## VideoRecorder Library

The VideoRecorder library captures MJPEG video and saves it as AVI files to the SD card.

**File:** `catcam/lib/VideoRecorder/src/VideoRecorder.h`

### Usage Example

```cpp
#include "VideoRecorder.h"

VideoRecorder recorder;

void setup() {
    // Camera must be initialized first
    camera->init();

    // Initialize video recorder
    recorder.init();
}

void recordVideo() {
    // Record with default settings (10 seconds, VGA 640x480, 10fps)
    VideoResult result = recorder.record();

    if (result.success) {
        Serial.printf("Video saved: %s (%d frames, %d bytes)\n",
            result.filename.c_str(), result.totalFrames, result.fileSize);
    } else {
        Serial.printf("Recording failed: %s\n", result.errorMessage.c_str());
    }
}

void recordCustomVideo() {
    // Record with custom settings
    VideoConfig config;
    config.frameSize = FRAMESIZE_SVGA;  // 800x600
    config.quality = 15;                 // JPEG quality
    config.fps = 15;                     // 15 frames per second
    config.durationSeconds = 5;          // 5 second video
    config.outputDir = "/videos";

    VideoResult result = recorder.record(config);
}

void recordWithProgress() {
    // Record with progress callback
    VideoConfig config = VideoRecorder::getDefaultConfig();

    VideoResult result = recorder.recordWithProgress(config,
        [](uint32_t current, uint32_t total, uint32_t elapsedMs) {
            Serial.printf("Recording: frame %d/%d (%.1fs)\n",
                current, total, elapsedMs / 1000.0);
        }
    );
}
```

### Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `frameSize` | `FRAMESIZE_VGA` (640x480) | Camera resolution |
| `quality` | 12 | JPEG quality (0-63, lower = better) |
| `fps` | 10 | Target frames per second |
| `durationSeconds` | 10 | Recording duration |
| `outputDir` | "/videos" | Output directory on SD card |

### Supported Frame Sizes

| Frame Size | Resolution | Notes |
|------------|------------|-------|
| `FRAMESIZE_QVGA` | 320x240 | Smallest, fastest |
| `FRAMESIZE_CIF` | 400x296 | |
| `FRAMESIZE_VGA` | 640x480 | **Recommended for video** |
| `FRAMESIZE_SVGA` | 800x600 | Good balance |
| `FRAMESIZE_XGA` | 1024x768 | Higher quality |
| `FRAMESIZE_HD` | 1280x720 | HD video |

### Technical Details

- **Format:** AVI container with MJPEG codec
- **Max Duration:** 30 seconds at 10fps (limited by frame index buffer)
- **Storage:** Videos saved to `/videos/video_{timestamp}.avi`
- **Camera Settings:** Temporarily changes camera settings during recording, restores original settings after
- **Playback:** Compatible with VLC, Windows Media Player, and most video players

## Git Branches

- `main` - Stable release
- `rebuild-catcam` - Arduino framework development
- `esp32s3-arduino-port` - ESP32-S3 support (Arduino framework)
- `claude-switch-to-espidf` - ESP-IDF experiment (archived)
