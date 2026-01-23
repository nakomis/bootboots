# BootBoots Minimal Bootloader (Factory App)

A minimal "factory" application that acts as a bootloader for the BootBoots CatCam ESP32 device.

## Architecture

This bootloader uses a **factory + single OTA partition** approach instead of dual OTA partitions, maximizing available flash space for the main application.

### Partition Layout

```
┌─────────────────────────────────────────────┐
│ Factory (448KB) - Bootloader App            │  0x10000-0x80000
├─────────────────────────────────────────────┤
│ OTA0 (3.56MB) - Main Application            │  0x80000-0x3F0000
├─────────────────────────────────────────────┤
│ SPIFFS (40KB) - Logging                     │  0x3F0000-0x3FA000
├─────────────────────────────────────────────┤
│ Coredump (24KB)                             │  0x3FA000-0x400000
└─────────────────────────────────────────────┘
```

**Comparison to Dual-Partition Approach:**
- **Old (dual partition)**: Two 1.94MB app partitions = 3.8MB total, 95% flash usage
- **New (factory + OTA)**: One 3.56MB app partition = ~61% flash usage, room to grow to ~3.4MB

## How It Works

### Boot Flow

1. **ESP32 boots** → Factory app runs (this bootloader)
2. **Check NVS flag** for pending OTA update:
   - **If NO pending update**: Set OTA0 as boot partition and reboot into main app
   - **If pending update**: Flash firmware from SD card to OTA0, then reboot
3. **Main app runs** (from OTA0 partition)
4. **Main app** sets boot partition back to factory (ensures bootloader runs on next reboot)

### Why Main App Sets Boot Partition to Factory

The ESP32 ROM bootloader doesn't automatically prefer the factory partition when otadata is blank - it boots the first valid app partition. Without intervention, the device would boot directly into OTA0, bypassing this bootloader entirely.

**Solution**: The main app calls `esp_ota_set_boot_partition(factory)` on every boot, ensuring this bootloader always runs first. The bootloader then decides whether to flash a pending update or boot into the main app.

### OTA Update Flow

1. **Main app** (in OTA0) receives OTA command via BLE
2. **Main app** downloads firmware to SD card (`/firmware_update.bin`)
3. **Main app** sets NVS flag `pending=true` and `size=<bytes>`
4. **Main app** reboots
5. **Bootloader** detects pending flag
6. **Bootloader** reads firmware from SD card
7. **Bootloader** flashes to OTA0 partition using ESP-IDF OTA API
8. **Bootloader** clears pending flag
9. **Bootloader** deletes firmware file from SD card
10. **Bootloader** sets OTA0 as boot partition and reboots
11. **New firmware** runs from OTA0

### Safety Features

- **NVS flag cleared before flash**: Prevents boot loops if flash fails
- **File existence checks**: Verifies firmware file on SD before attempting flash
- **Size validation**: Ensures firmware fits in OTA0 partition
- **Error recovery**: Always boots into main app on error
- **LED feedback**: Visual indication of bootloader state

## LED Blink Codes

- **3 quick blinks**: Bootloader starting
- **10 rapid blinks**: OTA update in progress
- **Single blink every second**: Bootloader shows progress during flash
- **5 quick blinks**: OTA update successful
- **Slow blink (1 sec interval)**: Critical error (system halted)

## Building & Flashing

### Quick Setup (Recommended)

Use the automated factory setup script for new devices:

```bash
cd ../catcam
./scripts/factory_setup.sh /dev/cu.usbmodem*
```

This script will:
1. Build both bootloader and catcam app
2. Flash bootloader to factory partition (0x10000)
3. Flash catcam app to OTA0 partition (0x80000)
4. Verify boot sequence

### Manual Flash (via USB)

If you need to flash just the bootloader:

```bash
cd /Users/martinmu_1/repos/nakomis/bootboots/embedded/bootloader
pio run -e esp32s3cam --target upload --upload-port /dev/cu.usbmodem*
```

This flashes the bootloader to the **factory** partition.

**Note**: You must also flash the main catcam app to OTA0 partition for the system to work.

### Building Main Application

The main application (`catcam`) must be built with a partition table that matches:
- OTA0 partition at 0x80000
- Maximum size: 3,604,480 bytes (3.56MB)

Update `catcam/partitions_custom.csv` to match `bootloader/partitions_bootloader.csv`.

### OTA Update Process

1. Main app downloads firmware to SD card
2. Sets NVS: `prefs.putBool("pending", true)` and `prefs.putUInt("size", <bytes>)`
3. Reboots
4. Bootloader handles the rest automatically

## NVS Namespace

The bootloader uses the `"ota"` NVS namespace with:
- **Key**: `pending` (bool) - Whether OTA update is pending
- **Key**: `size` (uint32) - Expected firmware size in bytes

## Dependencies

- **SD_MMC**: SD card access (1-bit mode)
- **Preferences**: NVS (Non-Volatile Storage) for OTA flag
- **esp_ota_ops.h**: ESP-IDF OTA API
- **esp_partition.h**: Partition management

## Serial Output

The bootloader outputs detailed logs via Serial (115200 baud):
- Boot detection
- OTA status
- Flash progress (every 10%)
- Error messages
- Reboot notifications

## File Structure

```
bootloader/
├── platformio.ini              # PlatformIO configuration
├── partitions_bootloader.csv   # Partition table
├── README.md                   # This file
├── include/
│   └── version.h              # Bootloader version
└── src/
    └── main.cpp               # Bootloader implementation
```

## Version

Current Version: **1.0.0**

## Notes

- **Factory partition is PERMANENT**: Once flashed, the factory app stays there forever. Only the main app (OTA0) can be updated via OTA.
- **SD card required**: OTA updates require an SD card to stage firmware files.
- **1-bit SD mode**: Uses 1-bit mode to minimize pin usage.
- **No WiFi/BLE**: Bootloader is minimal - no wireless connectivity, just flash and boot.
- **Fast boot**: If no pending OTA, bootloader boots into main app in ~1 second.

## Troubleshooting

### Bootloader won't flash
- Ensure device is in download mode (GPIO0 pulled low during boot)
- Check USB port matches `upload_port` in `platformio.ini`

### Bootloader boots into main app immediately
- This is normal if no pending OTA update
- Check NVS flag is set correctly by main app

### OTA update fails
- Check SD card is inserted and formatted (FAT32)
- Verify firmware file exists: `/firmware_update.bin`
- Check serial output for detailed error messages
- Ensure firmware size fits in OTA0 partition (3.8MB max)

### Boot loop
- Bootloader clears NVS flag before flashing to prevent boot loops
- If stuck, reflash bootloader via USB
- Check main app sets NVS flag correctly

## License

CC0 1.0 Universal - Public domain dedication.
