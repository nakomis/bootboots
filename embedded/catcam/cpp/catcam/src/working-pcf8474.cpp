// ESP32-CAM I2C Scanner and PCF8574 Test Program
#include <Arduino.h>
#include <Wire.h>

// Pins
#define TXD_PIN 1  // GPIO1 (U0TXD) - SDA
#define RXD_PIN 3  // GPIO3 (U0RXD) - SCL
#define SDA_PIN 12  // GPIO12 (U0RXD) - SDA
#define SCL_PIN 13  // GPIO13 (U0TXD) - SCL
#define I2C_CLOCK_SPEED 100000         // 100kHz for reliability
#define PCF8574_ADDRESS 0x20           // Default I2C address
#define TEST_LED_PIN 0                 // P0 on PCF8574

bool writePCF8574(uint8_t data);

void setup() {
    Serial.begin(115200);
    while (!Serial); // Wait for Serial to be ready
    // Initialize LED
    pinMode(TXD_PIN, OUTPUT);

    // Initialize button pin
    pinMode(RXD_PIN, INPUT_PULLDOWN);

}

void loop() {
    // Slow blink to show program is running
    if (digitalRead(RXD_PIN) == HIGH) {
        digitalWrite(TXD_PIN, LOW);
    }
    else {
        digitalWrite(TXD_PIN, HIGH);
    }

    // Wire.begin(SDA_PIN, SCL_PIN);
    Wire.begin(TXD_PIN, RXD_PIN);
    Wire.setClock(I2C_CLOCK_SPEED);

    // Test I2C communication with PCF8574
    Wire.beginTransmission(PCF8574_ADDRESS);
    uint8_t error = Wire.endTransmission();

    for (int i = 0; i < 10; i++) {
        if (error == 0) {
            writePCF8574(0x01); // Set all pins high to turn off LEDs
            delay(1000);
            writePCF8574(0x00); // Set all pins low to turn on LEDs
            delay(1000);
        }
        else {
            Serial.println("PCF8574 not found, retrying...");
            delay(1000);
        }
    }

    Wire.end();
}

bool writePCF8574(uint8_t data) {
    Wire.beginTransmission(PCF8574_ADDRESS);
    Wire.write(data);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}
