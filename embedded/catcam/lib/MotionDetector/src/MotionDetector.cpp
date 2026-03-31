#include "MotionDetector.h"
#include <SDLogger.h>

MotionDetector::MotionDetector(int pirPin)
    : _pirPin(pirPin)
    , _lastPinState(false)
    , _motionDetected(false)
    , _lastDebounce(0)
    , _cooldownStart(0)
    , _inCooldown(false)
{
    pinMode(_pirPin, INPUT);
}

void MotionDetector::update() {
    bool currentState = readRawState();
    unsigned long now = millis();

    // Check for rising edge (LOW -> HIGH transition)
    if (currentState && !_lastPinState) {
        // Potential motion detected - check debounce
        if ((now - _lastDebounce) >= DEBOUNCE_MS) {
            // Valid motion event - check cooldown
            if (!_inCooldown) {
                _motionDetected = true;
                SDLogger::getInstance().debugf("MotionDetector: Rising edge detected");
            } else {
                unsigned long remaining = getCooldownRemaining();
                SDLogger::getInstance().debugf("MotionDetector: Motion ignored - cooldown active (%lu ms remaining)", remaining);
            }
        }
        _lastDebounce = now;
    }

    _lastPinState = currentState;

    // Check if cooldown has expired
    if (_inCooldown && (now - _cooldownStart) >= COOLDOWN_MS) {
        _inCooldown = false;
        SDLogger::getInstance().debugf("MotionDetector: Cooldown expired - ready for new detection");
    }
}

bool MotionDetector::wasMotionDetected() {
    if (_motionDetected) {
        _motionDetected = false;

        // Start cooldown period
        _cooldownStart = millis();
        _inCooldown = true;
        SDLogger::getInstance().debugf("MotionDetector: Motion consumed - starting %lu ms cooldown", COOLDOWN_MS);

        return true;
    }
    return false;
}

bool MotionDetector::isInCooldown() const {
    return _inCooldown;
}

unsigned long MotionDetector::getCooldownRemaining() const {
    if (!_inCooldown) {
        return 0;
    }

    unsigned long elapsed = millis() - _cooldownStart;
    if (elapsed >= COOLDOWN_MS) {
        return 0;
    }
    return COOLDOWN_MS - elapsed;
}

void MotionDetector::resetCooldown() {
    _inCooldown = false;
    _cooldownStart = 0;
    SDLogger::getInstance().debugf("MotionDetector: Cooldown reset");
}

bool MotionDetector::readRawState() const {
    return digitalRead(_pirPin) == HIGH;
}
