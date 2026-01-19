#include "Camera.h"
#include "../../SDLogger/src/SDLogger.h"

Camera::Camera() {
    failureCount = 0;
}

void Camera::init() {
    SDLogger::getInstance().infof("Initializing ESP32-CAM...");
    
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    // High quality settings for AI inference
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA; // 1600x1200
        config.jpeg_quality = 10;
        config.fb_count = 2;
        SDLogger::getInstance().infof("PSRAM found - using high quality settings");
    } else {
        config.frame_size = FRAMESIZE_SVGA; // 800x600
        config.jpeg_quality = 12;
        config.fb_count = 1;
        SDLogger::getInstance().warnf("PSRAM not found - using lower quality settings");
    }
    
    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        SDLogger::getInstance().errorf("Camera init failed with error 0x%x", err);
        failureCount++;
        return;
    }
    
    // Get camera sensor for additional configuration
    sensor_t* s = esp_camera_sensor_get();
    if (s != nullptr) {
        // Optimize settings for AI inference
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 0);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 to 6 (0=No Effect)
        s->set_whitebal(s, 1);       // 0 = disable, 1 = enable
        s->set_awb_gain(s, 1);       // 0 = disable, 1 = enable
        s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled
        s->set_exposure_ctrl(s, 1);  // 0 = disable, 1 = enable
        s->set_aec2(s, 0);           // 0 = disable, 1 = enable
        s->set_ae_level(s, 0);       // -2 to 2
        s->set_aec_value(s, 300);    // 0 to 1200
        s->set_gain_ctrl(s, 1);      // 0 = disable, 1 = enable
        s->set_agc_gain(s, 0);       // 0 to 30
        s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
        s->set_bpc(s, 0);            // 0 = disable, 1 = enable
        s->set_wpc(s, 1);            // 0 = disable, 1 = enable
        s->set_raw_gma(s, 1);        // 0 = disable, 1 = enable
        s->set_lenc(s, 1);           // 0 = disable, 1 = enable
        s->set_hmirror(s, 0);        // 0 = disable, 1 = enable
        s->set_vflip(s, 0);          // 0 = disable, 1 = enable
        s->set_dcw(s, 1);            // 0 = disable, 1 = enable
        s->set_colorbar(s, 0);       // 0 = disable, 1 = enable
        
        SDLogger::getInstance().infof("Camera sensor configured for AI inference");
    }
    
    SDLogger::getInstance().infof("ESP32-CAM initialized successfully");
    failureCount = 0;
}

NamedImage* Camera::getImage() {
    // Flush stale frames from the buffer pool
    // With fb_count=2, old frames may be sitting in the queue
    for (int i = 0; i < 3; i++) {
        camera_fb_t* stale = esp_camera_fb_get();
        if (stale) {
            esp_camera_fb_return(stale);
        }
        delay(10);  // Brief delay to allow new frame capture
    }

    // Now capture the actual frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        SDLogger::getInstance().errorf("Camera capture failed");
        failureCount++;
        return nullptr;
    }

    if (fb->len == 0) {
        SDLogger::getInstance().errorf("Camera captured empty frame");
        esp_camera_fb_return(fb);
        failureCount++;
        return nullptr;
    }
    
    // Create NamedImage with timestamp
    NamedImage* namedImage = new NamedImage();
    if (!namedImage) {
        SDLogger::getInstance().errorf("Failed to allocate NamedImage");
        esp_camera_fb_return(fb);
        return nullptr;
    }
    
    // Generate filename with timestamp
    char filename[32];
    snprintf(filename, sizeof(filename), "img_%lu.jpg", millis());
    namedImage->filename = String(filename);
    
    // Copy image data to PSRAM if available, otherwise regular heap
    namedImage->image = copyToPSRAM(fb->buf, fb->len);
    namedImage->size = fb->len;
    
    // Return the camera frame buffer
    esp_camera_fb_return(fb);
    
    SDLogger::getInstance().debugf("Captured image: %s (%d bytes)", filename, namedImage->size);
    failureCount = 0;
    
    return namedImage;
}

void Camera::deInit() {
    esp_camera_deinit();
    SDLogger::getInstance().infof("Camera deinitialized");
}

void Camera::copyImageToPSRAM(NamedImage* namedImage) {
    if (!namedImage || !namedImage->image) {
        return;
    }
    
    // Image data is already copied in getImage() method
    // This method is kept for compatibility
}

void Camera::releaseImageBuffer(NamedImage* namedImage) {
    if (!namedImage) {
        return;
    }
    
    if (namedImage->image) {
        freePSRAM(namedImage->image);
        namedImage->image = nullptr;
        namedImage->size = 0;
    }
    
    delete namedImage;
}

uint8_t* Camera::copyToPSRAM(const uint8_t* src, size_t size) {
    if (!src || size == 0) {
        return nullptr;
    }
    
    uint8_t* dest = nullptr;
    
    // Try PSRAM first if available
    if (psramFound()) {
        dest = (uint8_t*)ps_malloc(size);
        if (dest) {
            memcpy(dest, src, size);
            SDLogger::getInstance().debugf("Allocated %d bytes in PSRAM", size);
            return dest;
        }
    }
    
    // Fallback to regular heap
    dest = (uint8_t*)malloc(size);
    if (dest) {
        memcpy(dest, src, size);
        SDLogger::getInstance().debugf("Allocated %d bytes in heap", size);
    } else {
        SDLogger::getInstance().errorf("Failed to allocate %d bytes for image buffer", size);
    }
    
    return dest;
}

void Camera::freePSRAM(uint8_t* ptr) {
    if (!ptr) {
        return;
    }
    
    // Check if pointer is in PSRAM range
    if (psramFound() && heap_caps_check_integrity(MALLOC_CAP_SPIRAM, true)) {
        free(ptr); // free() works for both PSRAM and regular heap
    } else {
        free(ptr);
    }
}