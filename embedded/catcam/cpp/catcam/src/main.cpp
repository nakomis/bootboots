#include <Arduino.h>
#include <Wire.h>
#include <esp_camera.h>

#include "main.h"
#include "secrets.h"
#include "WifiConnect.h"
#include "MessageQueue.h"
#include "Camera.h"

WifiConnect wifiConnect;
MessageQueue messageQueue;
Camera camera;

void setup() {
    Serial.begin(115200);
    Serial.println(BANNER);

    pinMode(4, INPUT);
    digitalWrite(4, LOW);
    rtc_gpio_hold_dis(GPIO_NUM_4);

    pinMode(GPIO_NUM_15, OUTPUT);
    digitalWrite(GPIO_NUM_15, HIGH);

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

    // Wait three seconds for the kitty to get in range
    Serial.println("Waiting for 3 seconds before taking picture...");
    delay(3000);

    // Initialize Camera
    camera.init();

    // Take Picture with Camera
    digitalWrite(GPIO_NUM_15, LOW);
    delay(10);
    NamedImage *namedImage = camera.getImage();
    if (!namedImage)
    {
        digitalWrite(GPIO_NUM_15, LOW);
        ESP.restart(); // Restart if no image is captured
    }
    digitalWrite(GPIO_NUM_15, HIGH);

    // Post Image to Message Queue
    Serial.println("Posting image to message queue...");
    wifiConnect.connect();
    messageQueue.postImage(namedImage);
    Serial.println("Image posted successfully");
    camera.deInit();
    Serial.println("Camera resources returned successfully");

    deepsleep();
}

void loop() {
  // Should never reach here, but if it does, go to deep sleep
  Serial.println("Loop reached, going to deep sleep...");
  deepsleep();
}

void deepsleep() {
    // Prepare for deep sleep
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    rtc_gpio_hold_en(GPIO_NUM_4);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);
    rtc_gpio_pullup_dis(GPIO_NUM_13);
    rtc_gpio_pulldown_en(GPIO_NUM_13);

    Serial.println("Taking a 3 minute nap before deep sleep...");
    delay(3 * 60 * 1000); // Sleep for 3 minutes before going to deep sleep
    Serial.println("Going to deep sleep now");
    esp_deep_sleep_start();
    Serial.println("This will never be printed");
}