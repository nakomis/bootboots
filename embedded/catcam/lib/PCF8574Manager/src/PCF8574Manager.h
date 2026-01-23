#ifndef CATCAM_PCF8574MANAGER_H
#define CATCAM_PCF8574MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "../../SDLogger/src/SDLogger.h"

class PCF8574Manager
{
public:
    // Pin definitions for PCF8574 (P0-P7)
    static constexpr uint8_t ATOMIZER_PIN = 0;      // P0 - Atomizer control
    static constexpr uint8_t FLASH_LED_PIN = 1;     // P1 - Flash LED
    static constexpr uint8_t PRESSURE_SENSOR_PIN = 2; // P2 - Pressure sensor input
    static constexpr uint8_t PIR_SENSOR_PIN = 3;    // P3 - PIR motion sensor (active HIGH)
    static constexpr uint8_t SPARE_PIN_4 = 4;       // P4 - Future expansion
    static constexpr uint8_t SPARE_PIN_5 = 5;       // P5 - Future expansion
    static constexpr uint8_t SPARE_PIN_6 = 6;       // P6 - Future expansion
    static constexpr uint8_t SPARE_PIN_7 = 7;       // P7 - Future expansion

    PCF8574Manager(uint8_t i2cAddress = 0x20);
    bool init(int sdaPin, int sclPin);
    
    // Safe pin control with validation
    bool setPinState(uint8_t pin, bool state);
    bool getPinState(uint8_t pin);
    bool readPinInput(uint8_t pin);
    
    // Specialized control functions
    bool setAtomizerState(bool active);
    bool setFlashLED(bool on);
    bool readPressureSensor();
    bool readPIRSensor();
    
    // System health and safety
    bool isConnected();
    bool performSelfTest();
    uint8_t getLastError();
    void resetErrorState();
    
    // Safety features
    void emergencyShutdown();
    bool isSafeToOperate();
    
private:
    uint8_t _i2cAddress;
    uint8_t _currentState;
    uint8_t _lastKnownState;
    bool _initialized;
    bool _connected;
    bool _emergencyMode;
    unsigned long _lastCommunication;
    uint8_t _lastError;
    uint8_t _consecutiveErrors;
    
    // Safety constants
    static constexpr unsigned long MAX_COMMUNICATION_TIMEOUT_MS = 5000;  // 5 seconds
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 3;
    static constexpr unsigned long SELF_TEST_INTERVAL_MS = 30000;        // 30 seconds
    
    // Error codes
    static constexpr uint8_t ERROR_NONE = 0;
    static constexpr uint8_t ERROR_I2C_TIMEOUT = 1;
    static constexpr uint8_t ERROR_I2C_NACK = 2;
    static constexpr uint8_t ERROR_DEVICE_NOT_FOUND = 3;
    static constexpr uint8_t ERROR_EMERGENCY_MODE = 4;
    static constexpr uint8_t ERROR_CONSECUTIVE_FAILURES = 5;
    
    // Internal I2C operations
    bool writeToDevice(uint8_t data);
    bool readFromDevice(uint8_t& data);
    bool validatePin(uint8_t pin);
    void updateConnectionStatus();
    void logError(const char* operation, uint8_t errorCode);
};

#endif
