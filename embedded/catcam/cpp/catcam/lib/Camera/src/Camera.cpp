#include "Camera.h"

Camera::Camera()
{
}

NamedImage* Camera::getImage()
{
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        return nullptr;
    }
    else
    {
        Serial.println("Camera capture succeeded");
        Serial.printf("Captured frame size: %zu bytes\n", fb->len);
        Serial.printf("Frame size: %dx%d, format: %d\n", fb->width, fb->height, fb->format);
    }
    Serial.println("Picture taken successfully, now saving to preferences...");
    preferences.begin("catcam", false);
    int pictureNum = preferences.getInt("pictureNum", 0);
    Serial.println("Current picture number: " + String(pictureNum));
    pictureNum++;
    preferences.putInt("pictureNum", pictureNum);
    Serial.println("Incremented picture number to: " + String(pictureNum));
    preferences.end();
    NamedImage* namedImage = new NamedImage();
    namedImage->filename = String(pictureNum) + ".jpeg";
    namedImage->image = fb->buf;
    namedImage->size = fb->len;
    return namedImage;
}

void Camera::deInit()
{
    esp_camera_return_all();
}

void Camera::init()
{
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
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.jpeg_quality = 0;
    config.fb_location = CAMERA_FB_IN_DRAM;

    if (psramFound())
    {
        Serial.println("PSRAM found, using UXGA resolution");
        config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
        config.jpeg_quality = 10;
        config.fb_count = 2;
    }
    else
    {
        Serial.println("No PSRAM found, using SVGA resolution");
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    // Init Camera
    Serial.println("Initializing camera...");
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        failureCount++;
        if (failureCount > 5)
        {
            Serial.println("Too many failures, resetting...");
            ESP.restart();
        }
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);
        rtc_gpio_pullup_dis(GPIO_NUM_13);
        rtc_gpio_pulldown_en(GPIO_NUM_13);

        Serial.println("Going to sleep now");
        delay(1000);
        esp_deep_sleep_start();

        return;
    }

    Serial.println("Camera initialized successfully, setting settings...");
    sensor_t* s = esp_camera_sensor_get();
    s->set_brightness(s, 2);     // -2 to 2
    s->set_whitebal(s, 1);      // 0 = disable, 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 4);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    s->set_exposure_ctrl(s, 1);
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
}