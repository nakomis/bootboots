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

## Phase 1: PCF8574 Manager (Foundation for Pin Expansion)

### 1.1 Copy PCF8574Manager Library
**Files to copy**:
- `old.catcam/lib/PCF8574Manager/` → `catcam/lib/PCF8574Manager/`

**Features**:
- I2C GPIO expansion (8 pins: P0-P7)
- Safety features: emergency shutdown, error tracking, self-test
- Pin assignments from old project:
  - P0: Atomizer control
  - P1: Flash LED
  - P2: Pressure sensor input
  - P3-P7: Spare (future expansion)

### 1.2 Update Dependencies
**platformio.ini**:
- Already has `Wire` library ✓
- Verify I2C speed: 100kHz for reliability

### 1.3 Integrate into Main
**src/main.cpp modifications**:
- Add `#include "PCF8574Manager.h"`
- Add global: `PCF8574Manager* pcfManager = nullptr;`
- Update `initializeHardware()`: Initialize I2C on GPIO1/GPIO3
- Update `initializeComponents()`: Initialize PCF8574 at address 0x20
- Update `SystemState.h`: Add `bool pcf8574Ready = false;`

### 1.4 Create Simple Test
**Test plan**:
- Initialize PCF8574
- Run self-test
- Blink LED on P1 (Flash LED pin)
- Verify via BLE status messages (since Serial won't work)
- **Note**: Must disconnect PCF8574 wiring to flash firmware

### 1.5 Documentation
- Update PLAN.md with PCF8574 status
- Document pin conflict and flashing procedure
- Create PCF8574_TESTING.md with wiring diagram

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

### 🔴 PCF8574 Flashing Procedure
1. **Before flashing**: Physically disconnect PCF8574 from GPIO1/GPIO3
2. **Flash firmware**: Via USB-serial
3. **After flashing**: Reconnect PCF8574 to GPIO1/GPIO3
4. **No Serial Monitor**: Cannot use after PCF8574 connected (pin conflict)
5. **Debugging**: Use BLE status messages and SD logging only

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

**Last Updated**: 2025-10-11

### ✅ Completed Infrastructure
- Bootloader + OTA system (factory + OTA0 partitions)
- Firmware cleanup Lambda (keeps 3 most recent versions in S3)
- BLE services (status + OTA commands)
- SD card logging with chunked transfer
- WiFi connectivity
- Web interface firmware manager

### 🔄 In Progress
- Phase 1: PCF8574 Manager (NEXT)

### ⏳ Pending
- Phase 2-7: Camera through Production

## Next Steps
1. Copy PCF8574Manager library to catcam/lib/
2. Update platformio.ini (if needed)
3. Modify main.cpp to initialize PCF8574
4. Create PCF8574 test with LED blink on P1
5. Build, flash (with PCF8574 disconnected), test (with PCF8574 connected)
6. Document results and proceed to Camera integration
