#include "Atomizer.h"

Atomizer::Atomizer(int controlPin) : _controlPin(controlPin), _isActive(false), _enabled(true), _lastActivation(0), _activationStart(0), _activationDuration(0), _consecutiveBootsDetections(0) {
}

void Atomizer::init() {
    pinMode(_controlPin, OUTPUT);
    digitalWrite(_controlPin, LOW);
    
    // Log to both Serial and SD card
    Serial.printf("Atomizer initialized on pin %d (SAFETY MODE: False positive avoidance priority)\n", _controlPin);
    Serial.printf("Safety thresholds: Boots >%.0f%%, Others <%.0f%%, Consecutive: %d\n", 
                  MIN_BOOTS_CONFIDENCE * 100, MAX_OTHER_CAT_CONFIDENCE * 100, REQUIRED_CONSECUTIVE_DETECTIONS);
    
    SDLogger::getInstance().infof("Atomizer initialized on pin %d (SAFETY MODE: Kappa protection priority)", _controlPin);
    SDLogger::getInstance().infof("Safety thresholds: Boots >%.0f%%, Others <%.0f%%, Consecutive: %d", 
                                  MIN_BOOTS_CONFIDENCE * 100, MAX_OTHER_CAT_CONFIDENCE * 100, REQUIRED_CONSECUTIVE_DETECTIONS);
}

bool Atomizer::shouldActivate(const DetectionResult& result) {
    if (!_enabled) {
        logRejection(result, "System manually disabled");
        return false;
    }
    
    if (!canActivate()) {
        logRejection(result, "Cooldown period active");
        return false;
    }
    
    if (!validateBootsDetection(result)) {
        _consecutiveBootsDetections = 0; // Reset counter
        return false;
    }
    
    if (!checkOtherCatConfidence(result)) {
        _consecutiveBootsDetections = 0; // Reset counter
        return false;
    }
    
    // Increment consecutive detections
    _consecutiveBootsDetections++;
    
    if (_consecutiveBootsDetections < REQUIRED_CONSECUTIVE_DETECTIONS) {
        logRejection(result, "Insufficient consecutive detections");
        return false;
    }
    
    return true;
}

void Atomizer::activate(int durationMs) {
    // Limit duration to maximum safe value
    if (durationMs > MAX_ACTIVATION_DURATION_MS) {
        durationMs = MAX_ACTIVATION_DURATION_MS;
        Serial.printf("Atomizer duration limited to %d ms for safety\n", MAX_ACTIVATION_DURATION_MS);
    }
    
    _activationDuration = durationMs;
    _activationStart = millis();
    _lastActivation = millis();
    _isActive = true;
    _consecutiveBootsDetections = 0; // Reset after activation
    
    digitalWrite(_controlPin, HIGH);
    Serial.printf("*** DETERRENT ACTIVATED *** Duration: %d ms\n", durationMs);
}

void Atomizer::deactivate() {
    if (_isActive) {
        digitalWrite(_controlPin, LOW);
        _isActive = false;
        unsigned long actualDuration = millis() - _activationStart;
        Serial.printf("Atomizer deactivated after %lu ms\n", actualDuration);
    }
}

bool Atomizer::isActive() {
    // Auto-deactivate if duration exceeded
    if (_isActive && (millis() - _activationStart) >= _activationDuration) {
        deactivate();
    }
    
    return _isActive;
}

bool Atomizer::canActivate() {
    return (millis() - _lastActivation) >= COOLDOWN_PERIOD_MS;
}

void Atomizer::logActivation(const DetectionResult& result) {
    // Log to Serial for immediate feedback
    Serial.printf("*** DETERRENT ACTIVATED ***\n");
    Serial.printf("Target: %s (%.1f%% confidence)\n", result.catName, result.confidence * 100);
    Serial.printf("All probabilities: [%.1f%%, %.1f%%, %.1f%%, %.1f%%, %.1f%%, %.1f%%]\n",
                  result.allProbabilities[0] * 100, result.allProbabilities[1] * 100,
                  result.allProbabilities[2] * 100, result.allProbabilities[3] * 100,
                  result.allProbabilities[4] * 100, result.allProbabilities[5] * 100);
    Serial.printf("Consecutive detections: %d\n", _consecutiveBootsDetections);
    Serial.printf("Activation time: %lu ms since boot\n", millis());
    
    // Log to SD card for persistent record
    SDLogger::getInstance().logDeterrentActivation(result.catName, result.confidence, result.allProbabilities);
    SDLogger::getInstance().criticalf("Consecutive detections: %d, Activation time: %lu ms", _consecutiveBootsDetections, millis());
}

void Atomizer::logRejection(const DetectionResult& result, const char* reason) {
    // Log to Serial for immediate feedback
    Serial.printf("Deterrent REJECTED: %s (%.1f%% conf) - %s\n", 
                  result.catName, result.confidence * 100, reason);
    
    // Log to SD card for persistent record
    SDLogger::getInstance().logDeterrentRejection(result.catName, result.confidence, reason);
}

void Atomizer::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
        deactivate(); // Stop any current activation
        _consecutiveBootsDetections = 0;
    }
    
    // Log to both Serial and SD card
    Serial.printf("Atomizer deterrent system: %s\n", enabled ? "ENABLED" : "DISABLED");
    SDLogger::getInstance().warnf("Atomizer deterrent system: %s", enabled ? "ENABLED" : "DISABLED");
}

bool Atomizer::isEnabled() {
    return _enabled;
}

bool Atomizer::validateBootsDetection(const DetectionResult& result) {
    if (result.index != 0) { // Boots is index 0
        logRejection(result, "Not Boots detection");
        return false;
    }
    
    if (result.confidence < MIN_BOOTS_CONFIDENCE) {
        logRejection(result, "Boots confidence too low");
        return false;
    }
    
    return true;
}

bool Atomizer::checkOtherCatConfidence(const DetectionResult& result) {
    // Check that no other cat has high confidence (avoid Kappa->Boots confusion)
    for (int i = 1; i < 6; i++) { // Skip index 0 (Boots)
        if (result.allProbabilities[i] > MAX_OTHER_CAT_CONFIDENCE) {
            logRejection(result, "Other cat confidence too high (possible confusion)");
            return false;
        }
    }
    return true;
}
