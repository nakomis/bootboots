# PCF8574 and Component Integration Plan

## Overview
Rebuild the BootBoots catcam project incrementally by bringing in components from old.catcam one at a time, starting with PCF8574 I2C GPIO expansion. This approach ensures each component works before adding complexity.

## Key Technical Constraints Identified

### Pin Configuration (ESP32-CAM)
- **I2C (PCF8574)**: GPIO1 (UART0 TX/SDA) + GPIO3 (UART0 RX/SCL)
  - ⚠️ **Critical**: Must disconnect during USB flashing (conflicts with serial)
  - Cannot use Serial Monitor (UART0) after PCF8574 is connected
  - Must use alternative debug output or BLE for monitoring
- **Camera**: Uses pins 0,5,18,19,21,22,23,25,26,27 (already constrained)
- **SD Card**: Uses SD_MMC pins (already in use)
- **Available for future**: GPIO16 mentioned as potentially usable

### Current Working State
✅ **Working**: OTA updates, BLE services, SD logging, WiFi
❌ **Missing**: PCF8574, Camera, Atomizer, HTTP Client, AI integration

## Phase 1: PCF8574 Manager (Foundation for Pin Expansion) ✅ CODE COMPLETE

### 1.1 Copy PCF8574Manager Library ✅
**Files copied**:
- `old.catcam/lib/PCF8574Manager/` → `catcam/lib/PCF8574Manager/` ✓

**Features**:
- I2C GPIO expansion (8 pins: P0-P7)
- Safety features: emergency shutdown, error tracking, self-test
- Pin assignments from old project:
  - P0: Atomizer control
  - P1: Flash LED ← **Currently blinking in test loop**
  - P2: Pressure sensor input
  - P3-P7: Spare (future expansion)

### 1.2 Update Dependencies ✅
**platformio.ini**:
- Already has `Wire` library ✓
- I2C speed: 100kHz for reliability ✓

### 1.3 Integrate into Main ✅
**src/main.cpp   difications** (COMPLETED):
- ✅ Added `#include <Wire.h>` and `#include "PCF8574Manager.h"`
- ✅ Added global: `PCF8574Manager* pcfManager = nullptr;`
- ✅ Added pin definitions: I2C_SDA (GPIO1), I2C_SCL (GPIO3), PCF8574_ADDRESS (0x20)
- ✅ Updated `initializeHardware()`:
  - **Enable internal pull-ups** on GPIO1/GPIO3 using `pinMode(pin, INPUT_PULLUP)`
  - Initialize I2C on GPIO1/GPIO3 at 100kHz
  - No external pull-up resistors needed! ✅
- ✅ Updated `initializeComponents()`: Initialize PCF8574 at address 0x20 with self-test
- ✅ Updated `SystemState.h`: Added `bool pcf8574Ready = false;`
- ✅ Updated system status log: Added PCF8574 status display

### 1.4 Create Simple Test ✅
**Test code integrated** (main.cpp loop()):
- ✅ Initialize PCF8574 with self-test
- ✅ Blink LED on P1 (Flash LED pin) every 2 seconds
- ✅ Log blink state via SDLogger (debug level)
- ✅ Verify via BLE status messages (Serial won't work with PCF8574 connected)
- ⚠️ **CRITICAL**: Must disconnect PCF8574 wiring from GPIO1/GPIO3 before flashing firmware

**Build Status**: ✅ **SUCCESS** (54.4% flash, 20.0% RAM)

### 1.5 Documentation ✅
- ✅ Created `PCF8574_TESTING.md` with complete testing guide
- ✅ Documented pin conflict (GPIO1/GPIO3 = UART0)
- ✅ Documented flashing procedure (disconnect before flash, reconnect after)
- ✅ Created wiring diagram
- ✅ Added troubleshooting section
- ✅ Updated PLAN.md with Phase 1 completion status

### 1.6 Current Status: **READY FOR HARDWARE TESTING** ⏳

**Next Steps**:
1. **Upload firmware to S3** (build and upload):
   ```bash
   python scripts/build_and_upload.py --version-type patch
   ```
2. **Wire PCF8574** according to PCF8574_TESTING.md:
   - Connect SDA to GPIO1, SCL to GPIO3
   - Connect power (3.3V) and ground
   - Set address pins (A0, A1, A2 all to GND = address 0x20)
   - **No external pull-up resistors needed** - internal pull-ups enabled automatically ✅
3. **Deploy firmware via OTA** (web interface → Firmware Manager → Deploy)
   - No need to disconnect PCF8574! OTA works wirelessly ✅
4. **Verify via web interface**:
   - Bluetooth tab → Connect to device
   - Check system status for "PCF8574: OK"
   - Click "Retrieve Logs" → Search for "PCF8574" initialization messages
   - All debugging via web interface - no need to remove SD card! ✅
5. **Visual test**: LED on P1 should blink every 2 seconds
6. **Update PLAN.md**: Mark Phase 1 as fully tested ✓

**Note**: Only disconnect PCF8574 for initial USB serial flash if OTA not yet available.

## Phase 2: Camera Module

### 2.1 Copy Camera Library
**Files to copy**:
- `old.catcam/lib/Camera/` → `catcam/lib/Camera/`
- `old.catcam/lib/NamedImage/` → `catcam/lib/NamedImage/` (dependency)

**Features**:
- ESP32-CAM image capture
- PSRAM buffer management
- Image buffer lifecycle (acquire/release)

### 2.2 Test Camera Independently
- Initialize camera with AI_THINKER pin configuration
- Capture test image
- Save to SD card: `/test_image_N.jpg`
- Verify image size and PSRAM usage
- Monitor via BLE status updates

### 2.3 Integration Points
- Add camera pointer to main.cpp
- Initialize after WiFi/BLE
- Add camera status to SystemState

## Phase 3: Flash LED Control (via PCF8574)

### 3.1 Test Flash LED
- Control PCF8574 P1 (Flash LED pin)
- Create simple blink pattern
- Coordinate with camera capture (flash during photo)

### 3.2 Safety Considerations
- Ensure flash doesn't interfere with camera capture timing
- Add flash enable/disable control

## Phase 4: HTTP Client for AI Integration

### 4.1 Copy HTTP Client
**Files to copy**:
- `old.catcam/lib/CatCamHttpClient/` → `catcam/lib/CatCamHttpClient/`
- `old.catcam/lib/AWSAuth/` → `catcam/lib/AWSAuth/` (if needed)

**Features**:
- HTTPS POST to AWS SageMaker
- SSL/TLS with certificate
- PSRAM optimization for memory

### 4.2 Test HTTP Client
- Create test endpoint or use existing API
- Send dummy image data
- Parse JSON response
- Verify memory usage (critical for ESP32-CAM)

### 4.3 Update Dependencies
**platformio.ini**:
- Already has `WiFiClientSecure` ✓
- Already has `ArduinoHttpClient` ✓
- Already has `ArduinoJson` ✓

## Phase 5: Atomizer Control (Deterrent System)

### 5.1 Copy Atomizer Library
**Files to copy**:
- `old.catcam/lib/Atomizer/` → `catcam/lib/Atomizer/`

**Features**:
- Ultra-safe activation logic
- Multi-layer validation for Kappa protection
- Confidence thresholds (90% Boots, <15% others)
- Cooldown period (60s)
- Consecutive detection requirement (2x)

### 5.2 Wire Atomizer via PCF8574
- Connect atomizer relay to PCF8574 P0
- Test activation via `pcfManager->setAtomizerState(true)`
- Verify safety shutdown works
- Test emergency stop

### 5.3 Integration
- Link Atomizer to PCF8574Manager
- Add atomizer control commands via BLE
- Test manual activation/deactivation from web interface

## Phase 6: Complete AI Detection Loop

### 6.1 Message Queue (Optional)
**Files to copy**:
- `old.catcam/lib/MessageQueue/` → `catcam/lib/MessageQueue/`
- Only if needed for async processing

### 6.2 Integrate Full Detection Pipeline
**Main loop flow**:
1. Capture image (Camera)
2. Send to AI service (HTTP Client)
3. Parse detection result (JSON)
4. Evaluate safety thresholds (Atomizer)
5. Activate deterrent if safe (PCF8574)
6. Log all events (SD Logger) ✓
7. Report via BLE (Bluetooth) ✓

### 6.3 Cat Recognition Setup
**From old project**:
- 6 cats: Boots(0), Chi(1), Kappa(2), Mu(3), Tau(4), Wolf(5)
- Boots = target (index 0)
- Kappa = protected (index 2, 67% misidentification rate)
- Thresholds:
  - Min 85% confidence for any detection
  - Min 90% for Boots activation
  - Max 15% for other cats when activating for Boots
  - Special: NEVER activate if detected cat index == 2 (Kappa hardcoded block)

## Phase 7: Testing & Refinement

### 7.1 End-to-End Test
- Complete detection cycle: camera → AI → decision → atomizer
- Test all cat scenarios (especially Kappa protection)
- Verify false positive prevention
- Monitor memory usage throughout cycle

### 7.2 Safety Validation
- Test PCF8574 emergency shutdown
- Verify atomizer cooldown works
- Test consecutive detection requirement
- Validate Kappa protection (index 2 hardcoded block)

### 7.3 Production Readiness
- Long-running stability test (24h+)
- Memory leak detection
- WiFi reconnection handling
- SD card full handling
- Create deployment checklist

## Critical Notes & Gotchas

### 🔴 PCF8574 Pin Conflict & Flashing
1. **OTA Updates (Recommended)**: ✅ No disconnection needed - flash wirelessly!
2. **USB Serial Flashing Only**: ⚠️ Physically disconnect PCF8574 from GPIO1/GPIO3 before flashing
3. **After USB Flash**: Reconnect PCF8574 to GPIO1/GPIO3 and power cycle
4. **Serial Monitor**: Cannot use after PCF8574 connected (GPIO1/GPIO3 pin conflict)
5. **Debugging**: Use BLE status messages and SD logging only
6. **Future Updates**: Always use OTA once PCF8574 is wired!

### ⚠️ Memory Constraints
- ESP32-CAM: Limited RAM (~38KB free with BLE active)
- Must use PSRAM for camera buffers
- HTTP client uses ~10KB
- BLE uses ~15-20KB
- Leave ~10KB safety margin

### 🛡️ Safety Features (Critical)
- Kappa protection: Hardcoded index check (never activate if index == 2)
- Multi-layer validation: confidence + other cats + consecutive detections
- Emergency shutdown: PCF8574 can force all outputs LOW
- Cooldown enforcement: 60s minimum between activations

### 📋 Testing Order Rationale
1. **PCF8574 first**: Foundation for all I/O expansion
2. **Camera second**: Image capture capability
3. **Flash LED third**: Simple PCF8574 control test
4. **HTTP fourth**: AI communication (complex)
5. **Atomizer fifth**: Safety-critical, test thoroughly
6. **Full integration last**: Complete detection loop

## Success Criteria

**Phase 1 (PCF8574)**:
- [ ] PCF8574 initializes successfully
- [ ] Self-test passes
- [ ] Can control all 8 pins
- [ ] Emergency shutdown works
- [ ] BLE reports status correctly

**Phase 2 (Camera)**:
- [ ] Camera initializes
- [ ] Can capture images
- [ ] Images save to SD card
- [ ] PSRAM usage monitored
- [ ] No memory leaks

**Phase 3-6 (Integration)**:
- [ ] Full detection cycle works
- [ ] AI responses parsed correctly
- [ ] Atomizer activates for Boots (when safe)
- [ ] Kappa NEVER triggers atomizer
- [ ] All events logged to SD

**Phase 7 (Production)**:
- [ ] 24h+ stability test passes
- [ ] No memory leaks detected
- [ ] All safety features validated
- [ ] False positive rate acceptable
- [ ] Complete documentation

## Current Status

**Last Updated**: 2025-10-11 (Phase 1 Code Complete)

### ✅ Completed Infrastructure
- Bootloader + OTA system (factory + OTA0 partitions)
- Firmware cleanup Lambda (keeps 3 most recent versions in S3)
- BLE services (status + OTA commands)
- SD card logging with chunked transfer
- WiFi connectivity
- Web interface firmware manager
- **PCF8574 Manager integration** (code complete, ready for hardware testing)

### 🔄 In Progress
- **Phase 1: PCF8574 Manager - Hardware Testing** ⏳
  - Code complete ✅
  - Build successful ✅
  - Documentation complete ✅
  - **Awaiting hardware test**: Flash firmware, wire PCF8574, verify LED blink

### ⏳ Pending
- Phase 2: Camera Module
- Phase 3: Flash LED Control
- Phase 4: HTTP Client for AI
- Phase 5: Atomizer Control
- Phase 6: Complete AI Detection Loop
- Phase 7: Testing & Refinement

## Next Steps
1. **Hardware Test Phase 1**:
   - Upload firmware to S3: `python scripts/build_and_upload.py --version-type patch`
   - Wire PCF8574 (no external pull-ups needed - internal pull-ups enabled automatically)
   - Deploy firmware via OTA (web interface → Firmware Manager)
     - **No disconnection needed!** OTA works wirelessly ✅
   - Verify via web interface (Bluetooth tab):
     - Check status for "PCF8574: OK"
     - Retrieve logs and search for "PCF8574" initialization
     - **No need to remove SD card!** All logs via web interface ✅
   - Visual test: LED on P1 blinks every 2 seconds
2. **After Phase 1 Success**:
   - Update PLAN.md with hardware test results
   - Remove/comment out LED blink test in loop()
   - Proceed to Phase 2: Camera Module integration

**Note**: The pin conflict only affects USB serial flashing, not OTA updates!
