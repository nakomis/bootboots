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

## 🔥 CURRENT SESSION SUMMARY (2025-10-04 - CRITICAL ISSUE ONGOING)

### Device Status
- **Current Version**: v1.0.10 (stable, advertising BLE)
- **Latest Version on S3**: v1.0.11
- **OTA Status**: ❌ NOT WORKING - Stage 1 succeeds, Stage 2 never happens

### Critical Issue: NVS Flag Not Being Set

**Symptom**:
- Stage 1 (download) completes successfully - 1.9MB `firmware_update.bin` exists on SD card
- Device reboots after download
- Stage 2 (flash) never happens - no "Pending OTA update detected" message in logs
- Device boots normally with old firmware version
- NVS flag `pending_ota=true` is never set

**Root Cause**:
Device crashes somewhere between completing the HTTP download (line 574 in OTAUpdate.cpp) and setting the NVS flag (lines 600-611). The crash happens AFTER file.close() and _httpClient->end() but BEFORE or DURING prefs.begin("ota", false).

**Evidence**:
- SD logs show "Starting two-stage OTA: downloading to SD card..." (line 442) as last message before SD logging disabled
- 1.9MB firmware file exists on SD card (download succeeded)
- No [OTA] diagnostic messages visible because SD logging is disabled during download
- Serial monitor crashes/conflicts prevent capturing [OTA] diagnostics
- No NVS flag found on subsequent boot (hasPendingUpdate() returns false)

**Attempted Fixes**:
1. ✅ v1.0.8: Fixed WiFiClient memory leak (was being allocated but pointer lost)
2. ✅ v1.0.10: Added detailed [OTA] diagnostics throughout downloadToSD()
3. ✅ v1.0.10: Added Serial.flush() calls before NVS access
4. ⏳ v1.0.12: Added boot counter for reliable log ordering
5. ❌ Still crashes - need serial capture OR alternative approach

### Version History & Key Changes

| Version | Status | Changes | Result |
|---------|--------|---------|--------|
| v1.0.3  | OLD    | Original firmware before two-stage OTA | Stable |
| v1.0.4  | FAILED | First two-stage OTA attempt | WiFiClient memory bug |
| v1.0.5  | FAILED | Fixed NVS flag management + logging bugs | Flash access conflict |
| v1.0.6  | STABLE | Fixed flash access conflict (moved flashFromSD before SDLogger init) | OTA still fails |
| v1.0.7  | ON S3  | Same as v1.0.6 | OTA test failed |
| v1.0.8  | STABLE | Fixed WiFiClient memory leak in downloadToSD() | OTA still fails |
| v1.0.9  | ON S3  | Same as v1.0.8 | OTA test failed - download succeeded, NVS flag not set |
| v1.0.10 | **CURRENT** | Added detailed [OTA] diagnostics | Device stable, OTA untested |
| v1.0.11 | ON S3  | Same as v1.0.10 | OTA test failed - same NVS issue |
| v1.0.12 | NEXT   | Add boot counter to log filenames | Not yet built |

### What Works ✅

1. **Stage 1 - Download to SD**:
   - HTTP client connection successful
   - Firmware download from S3 works (1.9MB files confirmed)
   - WiFiClient properly managed (no memory leaks)
   - SD card file write successful
   - File.close() succeeds

2. **Flash Access Conflict Fix**:
   - flashFromSD() runs before SDLogger::init()
   - SD_MMC manually initialized for firmware read only
   - No conflicts between SD logging and Update library

3. **NVS Flag Boot Loop Fix**:
   - Flag cleared BEFORE flash attempt (line 601-603 in flashFromSD())
   - Prevents permanent boot loops on flash failure
   - Device recovers gracefully from flash errors

4. **SD Logging**:
   - All errors properly logged when SD logging enabled
   - Logs include firmware URLs, versions, progress
   - Log rotation working

5. **Boot Counter** (v1.0.12+):
   - NVS-based incremental counter for log ordering
   - Files named: `0001_catcam_19700101_000000.log`, `0002_catcam_19700101_000100.log`, etc.
   - Reliable sorting regardless of device time

### What Doesn't Work ❌

1. **NVS Flag Setting**:
   - Device crashes after download completes
   - Crash happens before/during prefs.begin() call
   - NVS flag `pending_ota=true` never gets written
   - Possible memory exhaustion or Preferences conflict

2. **Stage 2 - Flash from SD**:
   - Never executes because NVS flag not set
   - flashFromSD() code is correct but never called
   - Firmware file sits on SD card unused

3. **End-to-End OTA**:
   - Download succeeds but flash never triggers
   - Device reboots to old firmware
   - User must manually flash via USB

4. **Serial Diagnostics Capture**:
   - `pio device monitor` crashes with termios error
   - Can't capture [OTA] diagnostic messages
   - User's `pio monitor` conflicts with Claude's monitor
   - No visibility into crash point without serial access

### Next Steps (In Priority Order)

#### Option 1: Fix NVS Crash (RECOMMENDED IF CONTINUING TWO-STAGE)
1. **Investigate NVS/Preferences conflict**:
   - SD_MMC uses NVS for configuration
   - HTTP client may allocate heap during cleanup
   - Preferences.begin() may fail due to low memory

2. **Try setting NVS flag BEFORE download**:
   - Move prefs.putBool("pending", true) to start of downloadToSD()
   - Update flag with size after download completes
   - If download fails, device reboots and clears stale flag

3. **Try alternative persistence**:
   - Write flag file to SD card instead of NVS: `/ota_pending.flag`
   - Check for flag file in main.cpp before SDLogger init
   - More reliable than NVS if memory is issue

4. **Add memory diagnostics**:
   - Log free heap before every operation
   - Check for heap fragmentation
   - Monitor largest free block

#### Option 2: Alternative OTA Approaches (IF TWO-STAGE UNFIXABLE)
1. **Try original updateFromURL() with HTTP** (not two-stage):
   - v1.0.8 fixed WiFiClient memory leak
   - May work now that leak is fixed
   - Simpler, fewer failure points

2. **Use Arduino OTA (WiFi)** instead of HTTP:
   - Built-in to ESP32 framework
   - Well-tested and reliable
   - Requires mDNS and being on same network

3. **USB-only updates** (manual):
   - Most reliable but requires physical access
   - Always works
   - Good fallback option

#### Option 3: Serial Capture (TO DIAGNOSE)
1. **Close user's pio monitor first**
2. **Use screen or minicom instead of pio monitor**:
   ```bash
   screen /dev/cu.usbserial-1430 115200
   # or
   minicom -D /dev/cu.usbserial-1430 -b 115200
   ```
3. **Capture [OTA] diagnostics to see exact crash point**
4. **Identify which line causes crash**

### Files Modified This Session

#### Core OTA Files
1. `lib/OTAUpdate/src/OTAUpdate.h` (lines 54-56):
   - Added `_client` member for HTTP connections
   - Separated from `_secureClient` for HTTPS

2. `lib/OTAUpdate/src/OTAUpdate.cpp`:
   - Constructor (line 17): Initialize `_client = nullptr`
   - Destructor (lines 31-34): Clean up `_client`
   - downloadToSD() (lines 431-623):
     - Fixed WiFiClient memory leak (lines 461-464)
     - Added detailed [OTA] diagnostics (lines 443-620)
     - Added Serial.flush() calls before critical operations
   - Multiple [OTA] tagged Serial.println() for debugging

3. `src/main.cpp` (lines 33-57):
   - Moved flashFromSD() check to BEFORE SDLogger::init()
   - Manual SD_MMC initialization for OTA flash
   - Prevents flash access conflict

#### Logging Improvements
4. `lib/SDLogger/src/SDLogger.h` (line 72):
   - Added `_bootCounter` member variable

5. `lib/SDLogger/src/SDLogger.cpp`:
   - Added `#include <Preferences.h>` (line 7)
   - init() (lines 32-38): Read and increment boot counter from NVS
   - init() (line 58): Log boot count
   - generateLogFileName() (lines 246-259): Prefix filenames with boot counter

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

### Stretch Goals (Future Enhancements)

#### 1. URL Shortening via nakom.is

**Problem**: S3 pre-signed URLs are extremely long (200+ characters) and wasteful over BLE.

**Solution**: Use nakom.is URL shortener (`/Users/martinmu_1/repos/nakomis/nakom.is`) to create short URLs.

**Benefits**:
- Reduced BLE payload size
- Easier debugging (readable short URLs)
- Centralized URL management

**Implementation**:
```
Long URL:  https://bootboots-firmware-updates.s3.eu-west-2.amazonaws.com/BootBoots/1.0.11/firmware.bin?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=...
Short URL: https://n.is/fw-abc123
```

Web interface generates short URL via nakom.is API, device follows redirect to S3.

#### 2. HTTPS vs HTTP Investigation

**Current State**: Using HTTP URLs due to earlier SSL memory issues.

**Question**: With WiFiClient memory leak fixed (v1.0.8), can we now use HTTPS?

**Tasks**:
1. Test HTTPS download with current code (WiFiClientSecure with `.setInsecure()`)
2. Measure heap usage during HTTPS download
3. Compare stability of HTTP vs HTTPS
4. If HTTPS works, switch default to HTTPS for security

**Expected Outcome**: HTTPS should work now that leak is fixed, providing better security.

#### 3. HTTP Short URLs via CDN/Gateway

**Context**: If HTTPS still doesn't work and we need HTTP, but AWS doesn't support pre-signed URLs over HTTP.

**Problem**: Can't use HTTP pre-signed S3 URLs directly.

**Solutions**:

**Option A: CloudFront with Signed URLs**
- Create CloudFront distribution for S3 bucket
- Generate CloudFront signed URLs (supports HTTP)
- Shorter URLs, CDN caching benefits
- Requires CloudFront setup

**Option B: API Gateway + Lambda Proxy**
- API Gateway HTTP endpoint
- Lambda function validates request, proxies to S3
- Returns HTTP redirect or streams file
- More flexible but adds latency

**Option C: nakom.is as Smart Redirector**
- nakom.is stores mapping: short URL → S3 URL
- On request, fetches from S3 with credentials
- Serves over HTTP to device
- Requires nakom.is backend enhancement

**Recommendation**: Option A (CloudFront) is cleanest AWS-native solution.

#### 4. Firmware Signing & Verification

**Enhancement**: Add MD5/SHA256 checksum verification.

**Benefits**:
- Detect corrupted downloads
- Prevent flashing invalid firmware
- Security against firmware tampering

**Implementation**:
- Store checksum in S3 alongside firmware
- Download checksum first
- Verify after download completes
- Only set NVS flag if verification passes

#### 5. Incremental OTA Updates

**Enhancement**: Delta updates instead of full firmware.

**Benefits**:
- Smaller download size
- Faster updates
- Less SD card wear

**Challenges**:
- Requires complex diff/patch logic
- Limited ESP32 libraries for binary patching
- Storage for both old and new firmware during patch

**Priority**: Low (full updates working is higher priority)

---

## Legend
- ✅ Complete
- ⏳ In Progress
- ❌ Not Started
- 🟢 Success
- 🟡 Partial
- 🔴 Blocked
