#include <Arduino.h>
#include <esp_camera.h>
#include <driver/rtc_io.h>
#include <Preferences.h>
#include <Wire.h>

#include "secrets.h"
#include "WifiConnect.h"
#include "MessageQueue.h"

WifiConnect wifiConnect;
MessageQueue messageQueue;
Preferences preferences;

// Pin definition for CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

int failureCount = 0;

void setup() {
    Serial.begin(115200);
    // Wait for the serial port to become available
    // delay(5000);

    Serial.println(BANNER);
    Serial.println("About to do pinMode stuff:");

    pinMode(4, INPUT);
    digitalWrite(4, LOW);
    rtc_gpio_hold_dis(GPIO_NUM_4);

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

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
    s->set_brightness(s, 0);     // -2 to 2
    s->set_whitebal(s, 1);      // 0 = disable, 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 4);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    s->set_exposure_ctrl(s, 1);

    camera_fb_t* fb = NULL;

    // Take Picture with Camera
    fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        return;
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

    // Post Image to Message Queue
    Serial.println("Posting image to message queue...");
    wifiConnect.connect();
    messageQueue.postImage(String(pictureNum) + ".jpeg", fb->buf, fb->len);
    Serial.println("Image posted successfully");
    esp_camera_return_all();
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    rtc_gpio_hold_en(GPIO_NUM_4);
    Serial.println("Camera resources returned successfully");

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);
    rtc_gpio_pullup_dis(GPIO_NUM_13);
    rtc_gpio_pulldown_en(GPIO_NUM_13);

    Serial.println("Going to sleep now");
    delay(1000);
    esp_deep_sleep_start();
    Serial.println("This will never be printed");
}

void loop() {

}
