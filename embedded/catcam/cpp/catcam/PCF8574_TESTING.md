# PCF8574 GPIO Expander - Testing Guide

## ⚠️ Pin Conflict with Serial/UART0

The PCF8574 uses GPIO1 (TX) and GPIO3 (RX) which are UART0 pins. This creates a conflict with USB serial communication.

**IMPORTANT**:
- **OTA Updates (Recommended)**: ✅ No disconnection needed - flash wirelessly via web interface
- **USB Serial Flashing**: ⚠️ You MUST physically disconnect the PCF8574 from GPIO1/GPIO3 before flashing
- **Serial Monitor**: Will NOT work once PCF8574 is connected (use BLE or SD logs instead)

## Hardware Setup

### PCF8574 Pin Connections

| PCF8574 Pin | ESP32-CAM Pin | Function |
|-------------|---------------|----------|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| SDA | GPIO1 (U0TXD) | I2C Data |
| SCL | GPIO3 (U0RXD) | I2C Clock |
| A0 | GND | Address bit 0 (address = 0x20) |
| A1 | GND | Address bit 1 |
| A2 | GND | Address bit 2 |

### Pull-up Resistors ✅

- **Internal pull-ups enabled** - No external resistors required! ✅
- The ESP32 enables ~45kΩ internal pull-ups on GPIO1 and GPIO3 via software
- This is configured automatically in `initializeHardware()` using `pinMode(pin, INPUT_PULLUP)`
- **Optional**: Add external 4.7kΩ pull-ups for more reliable I2C communication in noisy environments
  - Only needed for long wire runs (>10cm) or high-speed I2C (>100kHz)
  - Not required for this project

### PCF8574 GPIO Pins (P0-P7)

| Pin | Function | Test Output |
|-----|----------|-------------|
| P0 | Atomizer Control | Not used in Phase 1 |
| P1 | Flash LED | **Blinking in test** |
| P2 | Pressure Sensor | Not used in Phase 1 |
| P3-P7 | Spare | Future expansion |

## Flashing Procedure

### Option 1: OTA Update (RECOMMENDED - No Disconnection Required) ✅

**This is the preferred method once PCF8574 is connected:**

1. **Build and upload firmware**:
   ```bash
   python scripts/build_and_upload.py --version-type patch
   # or
   python scripts/build_and_upload.py --no-bump
   ```

2. **Deploy via web interface**:
   - Open web app → Firmware Manager
   - Connect to device via Bluetooth
   - Select the new firmware version
   - Click "Deploy Firmware Update"
   - Monitor progress via BLE

3. **Device updates automatically**:
   - Downloads firmware from S3 via WiFi
   - Reboots into bootloader
   - Flashes new firmware
   - Reboots with new version
   - **PCF8574 remains connected throughout** ✅

### Option 2: USB Serial Flashing (Only if OTA not available) ⚠️

**Only use this method for initial setup or if OTA fails:**

**Step 1: Disconnect PCF8574**
1. Physically disconnect PCF8574 SDA wire from GPIO1
2. Physically disconnect PCF8574 SCL wire from GPIO3
3. Leave power and ground connected if convenient, or disconnect entirely

**Step 2: Flash Firmware**
```bash
pio run --target upload --upload-port /dev/cu.usbserial-*
```

**Step 3: Reconnect PCF8574**
1. Reconnect SDA wire to GPIO1
2. Reconnect SCL wire to GPIO3
3. Power cycle the ESP32-CAM (unplug and replug USB)

## Testing Procedure

### Phase 1: Initialization Test

**Expected behavior after boot**:
1. ESP32-CAM boots normally
2. I2C initializes on GPIO1/GPIO3 at 100kHz
3. PCF8574 self-test runs (writes/reads test patterns)
4. System logs show PCF8574 initialization success

**Check via BLE Status**:
- Connect to device via web interface (Bluetooth tab)
- Look for "PCF8574 Manager initialized successfully" message
- System status should show `PCF8574: OK`

**Check via Web Interface Logs** (RECOMMENDED):
- Open web app → Bluetooth tab
- Connect to device
- Click "Retrieve Logs" button
- Logs are fetched via BLE (chunked transfer)
- Search for "PCF8574" in the log viewer

**Alternative - SD Card Logs** (if web interface unavailable):
```bash
# After test run, mount SD card and check logs
grep "PCF8574" /Volumes/FAT32_Mount/logs/*.log
```

Expected log entries:
```
[INFO] I2C initialized on GPIO1 (SDA) and GPIO3 (SCL)
[INFO] PCF8574Manager initializing on I2C address 0x20 (SDA: 1, SCL: 3)
[INFO] PCF8574: Performing self-test...
[INFO] PCF8574: Self-test completed successfully
[INFO] PCF8574Manager initialized successfully (SAFETY MODE: Emergency shutdown capable)
[INFO] PCF8574 Manager initialized - 8 GPIO pins available
```

### Phase 2: LED Blink Test

**Test code** (already integrated in main.cpp):
- Flash LED (P1) blinks every 2 seconds
- LED toggles ON/OFF continuously

**Visual verification**:
1. Connect LED to PCF8574 P1 with appropriate resistor (~330Ω)
2. LED should blink every 2 seconds
3. Observe for at least 30 seconds to confirm consistent blinking

**Log verification** (via web interface):
- Web app → Bluetooth tab → Retrieve Logs
- Search for "Flash LED" in log viewer
- Should see entries every 2 seconds

**Alternative** (SD card):
```bash
grep "Flash LED" /Volumes/FAT32_Mount/logs/*.log
```

Expected log entries (every 2 seconds):
```
[DEBUG] PCF8574 Flash LED (P1): ON
[DEBUG] PCF8574 Flash LED (P1): OFF
[DEBUG] PCF8574 Flash LED (P1): ON
...
```

### Phase 3: Emergency Shutdown Test

**Manual test** (add to main.cpp if needed):
```cpp
// Test emergency shutdown
pcfManager->emergencyShutdown();
// All pins should go LOW immediately
```

Expected behavior:
- All PCF8574 outputs forced to LOW
- Emergency mode flag set
- Future pin control blocked until reset

## Troubleshooting

### PCF8574 Not Detected

**Symptom**: Logs show "PCF8574 device not found at address 0x20"

**Solutions**:
1. Check I2C wiring (GPIO1=SDA, GPIO3=SCL)
2. Verify PCF8574 power (3.3V, not 5V)
3. Check PCF8574 address jumpers (A0, A1, A2 all to GND = 0x20)
4. **Optional**: Add external 4.7kΩ pull-ups if wires are long (>10cm)
   - Internal pull-ups (~45kΩ) are usually sufficient
   - External pull-ups help with long wires or noisy environments
5. Try I2C scanner code to detect address

### LED Not Blinking

**Symptom**: PCF8574 initializes but LED doesn't blink

**Solutions**:
1. Verify LED polarity (anode to P1, cathode to GND via resistor)
2. Check resistor value (~330Ω for standard LED)
3. Confirm P1 pin number (should be pin 1 on PCF8574)
4. Check logs for "Flash LED" debug messages
5. Measure voltage on P1 with multimeter (should toggle between 0V and 3.3V)

### Serial Monitor Not Working

**This is expected!** GPIO1/GPIO3 are used for I2C, not UART0.

**Alternative debugging**:
- Use BLE Bluetooth interface via web app
- Read SD card logs after test run
- Use GPIO33 onboard LED for visual feedback (if needed)

## Debugging Without Serial Monitor

Since Serial Monitor won't work with PCF8574 connected, use these methods:

### 1. Web Interface Logs (RECOMMENDED) ✅
- Connect via web interface → Bluetooth tab
- View real-time system status
- Click "Retrieve Logs" to fetch logs via BLE chunked transfer
- Search/filter logs directly in browser
- **No need to remove SD card!**

### 2. BLE Status Messages
- Real-time status updates every 10 seconds
- Shows PCF8574 initialization status
- Displays system health

### 3. SD Card Logging (Backup/Alternative)
- All events logged to `/logs/` on SD card
- Only needed if web interface unavailable
- Remove SD card and read logs on computer

### 4. LED Indicators
- PCF8574 P1 blinks = system working
- Onboard LED (GPIO33) available if needed

## Success Criteria

PCF8574 Phase 1 is complete when:
- [ ] PCF8574 initializes successfully
- [ ] Self-test passes
- [ ] P1 LED blinks every 2 seconds
- [ ] BLE status shows `PCF8574: OK`
- [ ] SD logs show successful initialization
- [ ] No I2C errors in logs
- [ ] Emergency shutdown tested (optional for Phase 1)

## Next Steps

After PCF8574 Phase 1 success:
1. Update PLAN.md with test results
2. Remove LED blink test from loop() (or comment out)
3. Proceed to Phase 2: Camera Module integration
4. Keep PCF8574 connected for future atomizer control (Phase 5)

## Wiring Diagram

```
ESP32-CAM                  PCF8574
┌────────────┐            ┌────────────┐
│            │            │            │
│  GPIO1 (TX)├────────────┤ SDA        │
│  GPIO3 (RX)├────────────┤ SCL        │
│            │            │            │
│   3.3V     ├────────────┤ VCC        │
│   GND      ├────────────┤ GND        │
│            │            │            │
└────────────┘            │ A0─────GND │
                          │ A1─────GND │
                          │ A2─────GND │
                          │            │
                          │ P0         │
                          │ P1─────[LED]──[330Ω]──GND
                          │ P2         │
                          │ P3-P7      │
                          └────────────┘

Internal pull-ups enabled via software (automatic):
   ESP32 GPIO1 (~45kΩ internal pull-up to 3.3V) ✅
   ESP32 GPIO3 (~45kΩ internal pull-up to 3.3V) ✅

Optional external pull-ups (only if needed for long wires):
   3.3V ──[4.7kΩ]── SDA
   3.3V ──[4.7kΩ]── SCL
```

## Important Notes

1. **Use OTA for all updates** once PCF8574 is wired - no disconnection needed! ✅
2. **Only disconnect for USB serial flashing** (initial setup or emergency recovery)
3. **Serial Monitor won't work** with PCF8574 connected - use BLE or SD logs instead
4. **Test LED should blink** every 2 seconds if working
5. **Emergency shutdown** is available for safety testing

## Reference

- PCF8574 Datasheet: NXP PCF8574/PCF8574A
- I2C Address: 0x20 (A0=A1=A2=GND)
- I2C Speed: 100kHz (set in code for reliability)
- Pull-up Resistors: 4.7kΩ recommended
- Supply Voltage: 3.3V (ESP32 compatible)
