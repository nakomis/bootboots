#include <Arduino.h>
#include <esp_camera.h>
#include <SDLogger.h>

SDLogger* sdLogger = nullptr;

void setup() {
    Serial.begin(115200);
    SDLogger::getInstance().init("/logs");
    SDLogger::getInstance().setLogLevel(LOG_DEBUG);
    LOG_I("=== BootBoots System Starting ===");
}

void loop() {
    SDLogger::getInstance().debug("Loop iteration");
    // put your main code here, to run repeatedly:
    delay(1000);
}
