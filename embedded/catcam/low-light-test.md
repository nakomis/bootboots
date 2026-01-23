# OV5640 Low Light Optimization Testing

Testing various camera settings to improve low-light performance on the ESP32-S3 with OV5640 sensor.

## Test Plan

| # | Setting | Change | Status | Result |
|---|---------|--------|--------|--------|
| 1 | Gain Ceiling | 0 → 6 (max) | ✅ Done | No improvement |
| 2 | Night Mode (AEC2) | 0 → 1 (enable) | ✅ Done | No improvement |
| 3 | AE Level | 0 → +2 | ✅ Done | No improvement |
| 4 | Exposure Value | 300 → 800 | ✅ Done | Slightly better, not good |
| 5 | Manual Gain | 0 → 15 | 🔄 Testing | - |
| 6 | Frame Size | UXGA → XGA | ⏳ Pending | - |
| 7 | JPEG Quality | 10 → 6 | ⏳ Pending | - |

## Test Details

### Test 1: Increase Gain Ceiling (0 → 6)
**Setting:** `s->set_gainceiling(s, (gainceiling_t)6)`

Allows auto-gain to amplify the signal much more in low light conditions.

**Result:** No improvement observed.

---

### Test 2: Enable Night Mode (AEC2)
**Setting:** `s->set_aec2(s, 1)`

Enables the OV5640's built-in night mode which automatically reduces frame rate in low light to allow longer exposures.

**Result:** No improvement observed.

---

### Test 3: Increase AE Level (0 → +2)
**Setting:** `s->set_ae_level(s, 2)`

Biases auto-exposure toward brighter images.

**Result:** No improvement observed.

---

### Test 4: Increase Exposure Value (300 → 800)
**Setting:** `s->set_aec_value(s, 800)`

Higher base exposure time (though auto-exposure may override).

**Result:** Slightly better, but not good.

---

### Test 5: Increase Manual Gain (0 → 15)
**Setting:** `s->set_agc_gain(s, 15)`

Manually set higher gain. Trade-off: more noise.

**Result:** Pending

---

### Test 6: Reduce Frame Size (UXGA → XGA)
**Setting:** `config.frame_size = FRAMESIZE_XGA` (1024x768)

Lower resolution = more light per pixel.

**Result:** Pending

---

### Test 7: Lower JPEG Quality Number (10 → 6)
**Setting:** `config.jpeg_quality = 6`

Preserves more detail (lower number = higher quality). Doesn't help with light but may preserve detail better in low-light images.

**Result:** Pending

---

## Notes

- All tests performed on ESP32-S3 with OV5640 camera
- Each test is done in isolation (reset to baseline before applying next change)
- Testing in consistent low-light indoor environment
