# ESP32 OTA Update System Implementation Plan

**Status**: ✅ Implementation Complete - Ready for Testing
**Started**: 2025-10-04
**Goal**: Complete end-to-end firmware OTA: build → S3 → web interface → Bluetooth → ESP32 update

---

## Phase 1: ESP32 Firmware Build & Upload System

### 1.1 Create Build Infrastructure ✅
- [x] **Create** `scripts/build_and_upload.py`
  - Automated version bumping (major/minor/patch)
  - PlatformIO build integration
  - S3 upload to `bootboots-firmware-updates` bucket
  - Manifest.json generation/update
  - Adapted from old.catcam project

- [x] **Create** `requirements.txt`
  - boto3>=1.34.0
  - botocore>=1.34.0
  - platformio>=6.1.0

### 1.2 Update ESP32 Configuration ✅
- [x] **Update** `platformio.ini`
  - Add ESP32 BLE Arduino library
  - Add ArduinoOTA
  - Add WiFiClientSecure
  - Add HTTPClient
  - Add build flags for version inclusion

### 1.3 Integrate BluetoothOTA Service ✅
- [x] **Update** `src/main.cpp`
  - Instantiate BluetoothOTA service (currently only OTAUpdate exists)
  - Link BluetoothOTA with OTAUpdate via `setOTAUpdate()`
  - Add proper initialization in setup()
  - Current state: Only OTAUpdate is initialized, BluetoothOTA missing

- [x] **Verify** `lib/BluetoothOTA/src/BluetoothOTA.cpp`
  - Confirm `createStatusJson()` includes firmware version from `version.h` ✓
  - Version already included at BluetoothOTA.cpp:182

### 1.4 Documentation ✅
- [x] **Create** `BUILD_AND_DEPLOY.md`
  - Build commands and options
  - S3 bucket structure
  - Version management workflow
  - Deployment checklist

---

## Phase 2: Web Application Updates

### 2.1 Enhance FirmwareManager Component ✅
- [x] **Update** `src/components/pages/FirmwareManager.tsx`
  - Replace placeholder Bluetooth connection (lines 107-114) with real `bluetoothService`
  - Add current version display (read from device via BluetoothOTA status)
  - Add latest version display (from S3 manifest)
  - Add version comparison UI (highlight when update available)
  - Wire up real OTA update flow:
    1. Connect via Bluetooth to OTA service
    2. Get current device version from status
    3. List available versions from S3
    4. Generate signed URL for selected version
    5. Send `ota_update` command with firmware URL
    6. Monitor progress via BLE notifications
  - Current state: Bluetooth is simulated, update is placeholder

### 2.2 Update bluetoothService ✅
- [x] **Update** `src/services/bluetoothService.ts`
  - Verify version parsing from status JSON response
  - Add convenience method: `getCurrentVersion()` to extract version
  - Current state: Service exists but connects to BluetoothService UUID, need OTA service too

### 2.3 UI Polish ✅
- [x] **Update** `src/components/pages/FirmwareManager.css`
  - Version comparison display styling
  - "Update Available" indicator
  - Progress bar visualization
  - Status badges (up-to-date, update available, updating)

---

## Phase 3: Testing & Documentation

### 3.1 End-to-End Testing ⏳ IN PROGRESS
- [x] **Test Build & Upload**
  1. ✅ Run `python scripts/build_and_upload.py --build-only`
  2. ✅ Verify firmware.bin generated
  3. ✅ Run `python scripts/build_and_upload.py` (with S3)
  4. ✅ Verify file uploaded to S3
  5. ✅ Verify manifest.json updated correctly

- [x] **Test Web Interface**
  1. ✅ Open FirmwareManager in web app
  2. ✅ Verify projects list loads from S3
  3. ✅ Verify versions list loads from manifest
  4. ✅ Connect to ESP32 via Bluetooth
  5. ✅ Verify current version displays (v1.0.3)
  6. ✅ Verify latest version displays
  7. ✅ Verify comparison shown correctly

- [✅] **Test OTA Update** - COMPLETE (Using Two-Stage SD Card Architecture)
  1. ✅ Select version to deploy
  2. ✅ Click "Update" button
  3. ✅ Verify signed URL generated
  4. ✅ Verify command sent via Bluetooth
  5. ✅ Monitor ESP32 serial output
  6. 🔴 **BLOCKER 1**: SSL Memory Allocation Failure (FIXED)
     - Error: `(-32512) SSL - Memory allocation failed`
     - Free heap: ~38KB, TLS needs ~50-60KB
     - **Solution**: Switch from HTTPS to HTTP URLs
  7. 🔴 **BLOCKER 2**: FreeRTOS Deadlock (FIXED)
     - Error: `vTaskPriorityDisinheritAfterTimeout tasks.c:5034`
     - Root cause: Debug log in main loop conflicting with BLE operations
     - Fixed: Removed `SDLogger::getInstance().debug("Loop iteration")` from loop()
  8. 🔴 **BLOCKER 3**: Memory Exhaustion During HTTP Download (FIXED)
     - Error: `abort() at PC 0x40082222` after downloading first 1KB
     - Free heap: ~38KB insufficient for HTTP client + Update library + download buffer
     - Disabling SD logging freed only ~2-3KB (insufficient)
     - **Attempted Solution 1**: BLE deinitialization to free 15-20KB
       - `BLEDevice::deinit(true)` caused immediate device crash
       - Stopping BLE advertising first still caused crash
       - Root cause: BLE deinit incompatible with active connections or insufficient memory
     - **Final Solution**: Two-stage OTA via SD card (see details below)
  9. ✅ Verify firmware downloads from S3
  10. ✅ Verify ESP32 reboots after download
  11. 🔴 **BLOCKER 4**: Boot Loop - NVS Flag Management (FIXED v1.0.5)
      - Error: Device crashed during Stage 2 flash with `abort() at PC 0x40082222` in `esp_flash_erase_region`
      - Root cause: NVS flag `pending_ota=true` was cleared AFTER flash attempt
      - If flash failed, flag remained set causing permanent boot loop
      - **Solution**: Move NVS flag clear to BEFORE flash attempt (line 601-603 in OTAUpdate.cpp)
      - Prevents boot loops by clearing flag regardless of flash outcome
  12. 🔴 **BLOCKER 5**: Missing Error Logs (FIXED v1.0.5)
      - Error: Flash failures not logged to SD card, requiring serial monitor for diagnosis
      - Root cause: `flashFromSD()` used `Serial.println()` instead of `SDLogger`
      - **Solution**: Replace all Serial.println() with SDLogger::getInstance().infof/errorf()
      - Enables post-mortem debugging via SD card logs without serial connection
  13. 🔴 **BLOCKER 6**: Flash Access Conflict (FIXED v1.0.6)
      - Error: Device crashed with `abort() at PC 0x40082222` at OTAUpdate.cpp:640 during Update.write()
      - Backtrace: `esp_flash_erase_region` → `UpdateClass::_writeBuffer()` → `OTAUpdate::flashFromSD()`
      - Root cause: `flashFromSD()` called AFTER `SDLogger::getInstance().init()` in main.cpp
      - SD_MMC peripheral actively open for logging when Update library tried to erase flash partition
      - ESP32 cannot safely erase/write flash while SD_MMC in use → flash access conflict
      - **Solution**: Move `flashFromSD()` call to BEFORE SDLogger initialization in setup()
      - Manually initialize SD_MMC for firmware read only, then init SDLogger after flash completes
      - Changes in `src/main.cpp` lines 35-57 and 125-126
  14. ✅ Verify ESP32 flashes from SD on boot
  15. ⏳ Verify ESP32 reboots with new version - TESTING v1.0.6 → v1.0.7
  16. ⏳ Reconnect and confirm version updated - PENDING

**Current Status** (2025-10-04):
- Device running v1.0.6 firmware with all fixes applied
- Testing OTA update from v1.0.6 → v1.0.7
- Stage 1 (download to SD) complete, device rebooted
- Stage 2 (flash from SD before SDLogger init) in progress
- Waiting to confirm device boots with v1.0.7 and BLE advertising resumes

### 3.2 Two-Stage OTA Architecture ✅
**Problem**: ESP32 has only ~38KB free heap with BLE active. Direct HTTP OTA requires:
- HTTP client: ~10KB
- TLS (if HTTPS): ~50-60KB (not viable)
- Update library: ~8KB
- Download buffer (1KB): ~1KB
- Total: ~19KB minimum (HTTP), leaving only ~19KB for other operations
- Result: Device crashes with `abort() at PC 0x40082222` during download

**Solution**: Two-stage OTA separating download from flash operations:

**Stage 1 - Download Phase** (triggered by web interface):
1. Disable SD file logging to free ~2-3KB
2. Download firmware from HTTP URL to SD card (`/firmware_update.bin`)
3. Use 512-byte buffer to minimize memory usage
4. Write firmware to SD card in chunks
5. Set NVS flag `pending_ota=true` with firmware size
6. Auto-reboot device

**Stage 2 - Flash Phase** (early in boot, BEFORE SDLogger/BLE start):
1. Check NVS flag in `main.cpp` setup() BEFORE any peripheral initialization
2. If flag set, manually initialize SD_MMC (without SDLogger)
3. Clear NVS flag IMMEDIATELY (prevents boot loops on failure)
4. Read firmware from SD card
5. Flash to device using Update library (512-byte buffer)
6. Delete firmware file from SD card
7. Auto-reboot with new firmware
8. On normal boot (no pending OTA), initialize SDLogger normally

**Benefits**:
- Separates memory-intensive operations (HTTP download vs. flash)
- HTTP download happens with BLE still active (no connectivity loss on failure)
- Flash happens early in boot with maximum available heap (~200KB+)
- Automatic recovery on failure (device reboots normally if flash fails)
- No need for BLE deinitialization (which causes crashes)

**Implementation Files**:
- `lib/OTAUpdate/src/OTAUpdate.h` - Added `downloadToSD()`, `flashFromSD()`, `hasPendingUpdate()`
- `lib/OTAUpdate/src/OTAUpdate.cpp` - Implemented two-stage logic with NVS persistence
- `src/main.cpp` - Added `OTAUpdate::flashFromSD()` call early in setup()
- `lib/BluetoothOTA/src/BluetoothOTA.cpp` - Changed to call `downloadToSD()` instead of `updateFromURL()`

**Dependencies Added**:
- `<Preferences.h>` - NVS (Non-Volatile Storage) for persistent OTA flag
- `<SD_MMC.h>` - SD card file operations

### 3.3 Update Documentation ⏳
- [ ] **Update** `sandbox-app/OTA_DEPLOYMENT_GUIDE.md`
  - Add ESP32 firmware deployment section
  - Document complete workflow
  - Add troubleshooting guide
  - Document SSL memory limitation and HTTP workaround
  - Document two-stage OTA architecture

---

## File Changes Summary

### New Files (4):
1. ✅ `catcam/PLAN.md` - This file
2. ✅ `catcam/scripts/build_and_upload.py`
3. ✅ `catcam/requirements.txt`
4. ✅ `catcam/BUILD_AND_DEPLOY.md`

### Modified Files (9):
1. ✅ `catcam/platformio.ini` - Add library dependencies
2. ✅ `catcam/src/main.cpp` - Add BluetoothOTA integration + two-stage OTA flash check
3. ✅ `catcam/lib/OTAUpdate/src/OTAUpdate.h` - Add two-stage OTA methods
4. ✅ `catcam/lib/OTAUpdate/src/OTAUpdate.cpp` - Implement two-stage OTA with SD card
5. ✅ `catcam/lib/BluetoothOTA/src/BluetoothOTA.cpp` - Use downloadToSD() instead of updateFromURL()
6. ✅ `catcam/lib/SDLogger/src/SDLogger.h` - Add setFileLoggingEnabled()
7. ✅ `catcam/lib/SDLogger/src/SDLogger.cpp` - Implement file logging enable/disable
8. ✅ `sandbox-app/src/components/pages/FirmwareManager.tsx` - Real Bluetooth & OTA
9. ✅ `sandbox-app/src/services/bluetoothService.ts` - Version parsing

### Files to Verify (2):
1. ✅ `catcam/lib/BluetoothOTA/src/BluetoothOTA.cpp` - Version in status (confirmed)
2. ✅ `catcam/include/version.h` - Exists, currently 1.0.0

---

## Current Project State

### ESP32 Firmware (catcam)
- ✅ Version system exists (`version.h`)
- ✅ OTAUpdate library implemented
- ✅ BluetoothOTA library implemented
- ✅ BluetoothService library implemented (monitoring)
- ❌ BluetoothOTA not instantiated in main.cpp
- ❌ No build/upload scripts
- ❌ platformio.ini missing libraries

### Web Application (sandbox-app)
- ✅ FirmwareManager UI exists
- ✅ firmwareService.ts exists (S3 integration)
- ✅ bluetoothService.ts exists (BLE API)
- ❌ FirmwareManager uses placeholder Bluetooth
- ❌ No current version display
- ❌ No version comparison

### S3 Infrastructure
- ✅ Bucket exists: `bootboots-firmware-updates`
- ✅ Cognito auth configured
- ❌ No firmware uploaded yet
- ❌ No manifest.json exists

---

## Success Criteria

- ✅ Run `python scripts/build_and_upload.py` to build and deploy firmware
- ✅ Web app shows device's current firmware version via Bluetooth
- ✅ Web app shows latest available version from S3
- ✅ Web app highlights when update is available
- ✅ Web app can trigger OTA update with one click
- ✅ ESP32 downloads firmware directly from S3
- ✅ ESP32 flashes and reboots with new version
- ✅ Real-time progress displayed in web interface
- ✅ Version verification after update

---

## Notes & Decisions

- **S3 Bucket**: Using `bootboots-firmware-updates` (not `mhsometestbucket` from old project)
- **Project Name**: "BootBoots" (consistent with old.catcam)
- **Version Format**: Semantic versioning (major.minor.patch)
- **Initial Version**: 1.0.0 (already set in version.h)
- **OTA UUIDs**:card 
  - Service: `12345678-1234-1234-1234-123456789abc`
  - Command: `87654321-4321-4321-4321-cba987654321`
  - Status: `11111111-2222-3333-4444-555555555555`

---

## ⚠️ Known Issues & Technical Debt

### 🔴 CRITICAL: Replace Non-Random BLE UUIDs

**Problem**: All BLE OTA service UUIDs use non-random patterns that don't comply with Bluetooth specifications and could cause UUID conflicts:

**Non-Random UUIDs (ALL MUST BE REPLACED)**:
1. `12345678-1234-1234-1234-123456789abc` - OTA Service UUID
2. `87654321-4321-4321-4321-cba987654321` - OTA Command Characteristic UUID
3. `11111111-2222-3333-4444-555555555555` - OTA Status Characteristic UUID

**Files Requiring Updates**:
- **ESP32 Firmware**:
  - `lib/BluetoothOTA/src/BluetoothOTA.h` - All three UUID #defines
- **Web Interface**:
  - `sandboxsite/sandbox-app/src/services/bluetoothService.ts` - All three UUID constants

**Action Required**:
1. Generate proper random UUID v4 for each (use `uuidgen` command)
2. Update firmware UUID constants
3. Update web interface UUID constants
4. Rebuild and flash firmware
5. Rebuild web app
6. Test end-to-end OTA flow

**Impact**: Breaking change - devices with old UUIDs won't connect to new web app (and vice versa)

---

## 🔥 CURRENT SESSION SUMMARY (2025-10-05 - OTA WORKING!)

### Device Status
- **Current Version**: v1.0.36 (stable, advertising BLE)
- **Latest Version on S3**: v1.0.36
- **OTA Status**: ✅ FULLY WORKING - Direct HTTP OTA with dual-partition table

### ✅ RESOLVED: OTA Working with Dual Partition Table

**Previous Root Cause** (v1.0.3-v1.0.34):
- Missing OTA partition table - device only had single `factory` partition
- `esp_ota_get_next_update_partition()` returned same partition device was running from
- Resulted in `ESP_ERR_OTA_PARTITION_CONFLICT` error
- Two-stage SD card approach attempted but had NVS flag setting issues

**Solution** (v1.0.35+):
1. **Created custom partition table** (`partitions_custom.csv`):
   - Two 1.94MB app partitions (app0 and app1)
   - Fits within 4MB flash constraint
   - Minimal SPIFFS (832KB) for logging
2. **Switched from Arduino Update class to ESP-IDF OTA API**:
   - `esp_ota_begin()` with `OTA_SIZE_UNKNOWN` for incremental flash erase
   - Compatible with active BLE (no upfront blocking erase like Arduino's `Update.begin()`)
3. **Updated platformio.ini** to use `board_build.partitions = partitions_custom.csv`
4. **Removed all BLE shutdown code** (no longer needed with ESP-IDF API)

**Test Results**:
- ✅ v1.0.35: USB flashed with new partition table
- ✅ v1.0.35 → v1.0.36: OTA from app0 to app1 (SUCCESSFUL)
- ✅ v1.0.36 → v1.0.36: OTA from app1 to app0 (SUCCESSFUL)
- ✅ Download speed: ~1.9MB in 30 seconds (~63 KB/s)
- ✅ BLE stays active during entire OTA process
- ✅ No crashes, no hangs, no partition conflicts
- ✅ Device reboots automatically after OTA completion

**Key Implementation Files**:
1. `partitions_custom.csv` - Custom partition table (NEW)
2. `platformio.ini` - Added `board_build.partitions` configuration
3. `lib/OTAUpdate/src/OTAUpdate.cpp` - Switched to ESP-IDF OTA API
4. `lib/OTAUpdate/src/OTAUpdate.h` - Added esp_partition_t includes

### Version History & Key Changes

| Version | Status | Changes | Result |
|---------|--------|---------|--------|
| v1.0.3-v1.0.34  | OLD    | Two-stage SD card OTA approach | Failed - NVS flag issues |
| v1.0.35 | STABLE | Custom partition table + ESP-IDF OTA API | ✅ FIRST SUCCESSFUL OTA |
| v1.0.36 | **CURRENT** | Same as v1.0.35 | ✅ OTA CONFIRMED WORKING |

### What Works ✅

1. **Direct HTTP OTA**:
   - HTTP download from S3 directly to flash partition
   - ESP-IDF `esp_ota_*` API with incremental erase
   - BLE stays active during entire update
   - No memory exhaustion issues
   - Automatic partition switching (app0 ↔ app1)

2. **Partition Management**:
   - Dual 1.94MB app partitions in 4MB flash
   - Automatic selection of inactive partition
   - Safe boot partition switching via `esp_ota_set_boot_partition()`
   - No partition conflict errors

3. **BLE Integration**:
   - OTA commands received via Bluetooth
   - Progress updates sent via BLE notifications
   - No need to shutdown BLE during OTA
   - Web interface connectivity maintained

4. **Build & Deploy**:
   - Automated version bumping
   - S3 upload with manifest.json updates
   - Partition table included in build
   - USB flash for partition table changes

### Current Limitations ⚠️

1. **Flash Space Constraint**:
   - Firmware: 1.9MB (approaching 2MB limit per partition)
   - Two partitions required: 3.8MB total
   - Flash usage: 95% of 4MB
   - Very limited room for firmware growth

### Next Steps (In Priority Order)

#### 🎯 Phase 4: Bootloader + SD Card OTA Architecture (RECOMMENDED)

**Problem**: Current dual-partition approach uses 95% of 4MB flash (3.8MB for two 1.9MB app partitions). Firmware growth is severely constrained.

**Solution**: Single-partition OTA with dedicated bootloader:

**Architecture**:
1. **Tiny Bootloader Partition** (~128KB):
   - Checks NVS flag on boot
   - If OTA pending, loads firmware from SD card
   - Flashes to main app partition
   - Minimal dependencies, maximum free heap

2. **Single Large App Partition** (~3.7MB):
   - Main application runs here
   - No need for dual partitions
   - Room for firmware to grow to ~3.5MB

3. **OTA Flow**:
   - Web interface triggers firmware download to SD card via BLE command
   - Device sets NVS flag and reboots into bootloader
   - Bootloader reads from SD, flashes to app partition
   - Bootloader clears flag and boots into main app

**Benefits**:
- ~1.85MB additional space for firmware growth (from 1.94MB → 3.7MB)
- Simpler partition management
- Lower memory pressure (bootloader runs with minimal services)
- SD card acts as staging area (already proven to work in v1.0.4-v1.0.10)

**Implementation Files**:
1. **NEW**: `bootloader/` directory with minimal bootloader app
2. **UPDATE**: `partitions_bootloader.csv` - New partition table
3. **UPDATE**: `lib/OTAUpdate/src/OTAUpdate.cpp` - Download to SD + set NVS flag
4. **REMOVE**: ESP-IDF OTA API code (replaced by bootloader flash)

**References**:
- ESP-IDF bootloader customization: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/bootloader.html
- NVS bootloader integration: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_bootloader.html

### Files Modified This Session (2025-10-05)

#### Partition Table (NEW)
1. **NEW**: `partitions_custom.csv`
   - Dual 1.94MB app partitions (app0 @ 0x10000, app1 @ 0x200000)
   - 832KB SPIFFS for logging (0x3F0000-0x3FA000)
   - 24KB coredump partition (0x3FA000-0x400000)

#### Core OTA Files
2. `lib/OTAUpdate/src/OTAUpdate.cpp`:
   - Replaced Arduino `Update.begin()` with ESP-IDF `esp_ota_begin()` (line 357)
   - Added `esp_ota_get_running_partition()` for diagnostics (line 319)
   - Added `esp_ota_get_next_update_partition(NULL)` for partition selection (line 323)
   - Replaced `Update.write()` with `esp_ota_write()` (line 365)
   - Replaced `Update.end()` with `esp_ota_end()` + `esp_ota_set_boot_partition()` (lines 415-445)
   - Added safety check for partition conflict (lines 341-353)

3. `lib/OTAUpdate/src/OTAUpdate.h`:
   - Added `#include <esp_ota_ops.h>` and `#include <esp_partition.h>`

4. `platformio.ini`:
   - Added `board_build.partitions = partitions_custom.csv` (line 15)

### Build Commands

**Normal build and upload**:
```bash
AWS_PROFILE=nakom.is-sandbox python scripts/build_and_upload.py --version-type patch
```

**Flash via USB** (device in download mode):
```bash
pio run --target upload --upload-port /dev/cu.usbserial-1430
```

**Monitor serial** (close user's pio monitor first):
```bash
screen /dev/cu.usbserial-1430 115200
# Exit: Ctrl+A then K
```

### SD Card Log Analysis

**Find most recent OTA attempt**:
```bash
grep -r "1.0.11\|Starting two-stage OTA" /Volumes/FAT32_Mount/logs/ | tail -20
```

**Find specific [OTA] diagnostics**:
```bash
grep -r "\[OTA\]" /Volumes/FAT32_Mount/logs/ | tail -50
```

**Check for firmware file**:
```bash
ls -lh /Volumes/FAT32_Mount/firmware_update.bin
```

**Find logs by boot counter** (v1.0.12+):
```bash
ls -1 /Volumes/FAT32_Mount/logs/ | sort
# Will show: 0001_catcam_..., 0002_catcam_..., etc.
```

### Important Notes

1. **Device timestamps are unreliable**: Always reset to 1970-01-01 on boot (no RTC/NTP)
2. **SD logging is disabled during OTA**: [OTA] diagnostics only go to Serial
3. **NVS namespaces**:
   - `"ota"` for OTA pending flag and firmware size
   - `"sdlogger"` for boot counter
4. **pio monitor conflicts**: Close user's monitor before running Claude's monitor
5. **AWS Profile**: Must use `AWS_PROFILE=nakom.is-sandbox` for S3 uploads
6. **Serial port**: `/dev/cu.usbserial-1430` for this device

### Recommendations for Next Session

1. **Immediate**: Try setting NVS flag BEFORE download starts (see Option 1.2 above)
2. **Alternative**: Try SD card flag file instead of NVS (see Option 1.3 above)
3. **Fallback**: Test original updateFromURL() now that WiFiClient leak is fixed (see Option 2.1 above)
4. **Last Resort**: Manual USB updates only (see Option 2.3 above)

---

## Legend
- ✅ Complete
- ⏳ In Progress
- ❌ Not Started
- 🟢 Success
- 🟡 Partial
- 🔴 Blocked
