# Claude Code Context - BootBoots CatCam

## Project Overview

This project contains firmware for the BootBoots CatCam system:
- **bootloader/** - Arduino-based factory bootloader (checks SD card for OTA updates)
- **catcam/** - Arduino-based main application (camera, WiFi, BLE, SD logging)

Both use the **Arduino framework** for simplicity and library compatibility.

## Supported Boards

### ESP32-S3 WROOM N16R8 CAM (Primary Target)
- 16MB Flash, 8MB PSRAM
- Dual USB-C ports: UART (serial monitor) and OTG (flashing)
- Build environment: `esp32s3cam`

### Original ESP32-CAM AI-Thinker (Legacy)
- 4MB Flash, 4MB PSRAM
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

// LED
LED_PIN     = GPIO 48
```

### Original ESP32-CAM (AI-Thinker)
```cpp
// SD Card (SDMMC 4-bit mode, conflicts with some GPIOs)
// I2C shares UART0 pins (GPIO 1/3) - requires disabling serial

// LED
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

## Flash Commands

### Full Flash (both bootloader and catcam)

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

### Flash Only Main App (catcam)

**Remember:** Close the serial monitor first, then put device in download mode.

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port /dev/cu.usbmodem* --baud 921600 \
  write_flash -z --flash_mode dout --flash_freq 80m --flash_size 16MB \
  0x110000 catcam/.pio/build/esp32s3cam/firmware.bin
```

### Serial Monitor

After flashing, unplug from OTG port and plug into **UART port**:

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
│       ├── OTAUpdate/
│       ├── PCF8574Manager/
│       ├── SDLogger/
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

## Git Branches

- `main` - Stable release
- `rebuild-catcam` - Arduino framework development
- `esp32s3-arduino-port` - ESP32-S3 support (Arduino framework)
- `claude-switch-to-espidf` - ESP-IDF experiment (archived)
