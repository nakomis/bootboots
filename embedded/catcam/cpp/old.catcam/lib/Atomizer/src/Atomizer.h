#ifndef CATCAM_ATOMIZER_H
#define CATCAM_ATOMIZER_H

#include <Arduino.h>
#include "../../SDLogger/src/SDLogger.h"

struct DetectionResult {
    const char* catName;
    float confidence;
    int index;
    float* allProbabilities; // Array of all 6 cat probabilities
};

class Atomizer
{
public:
    Atomizer(int controlPin);
    void init();
    bool shouldActivate(const DetectionResult& result);
    void activate(int durationMs = 2000);
    void deactivate();
    bool isActive();
    bool canActivate(); // Check cooldown period
    void logActivation(const DetectionResult& result);
    void logRejection(const DetectionResult& result, const char* reason);
    void setEnabled(bool enabled); // Manual override
    bool isEnabled();

private:
    int _controlPin;
    bool _isActive;
    bool _enabled;
    unsigned long _lastActivation;
    unsigned long _activationStart;
    int _activationDuration;
    int _consecutiveBootsDetections;
    
    // Safety thresholds - prioritizing false positive avoidance
    static constexpr float MIN_BOOTS_CONFIDENCE = 0.90f;      // 90% minimum for Boots
    static constexpr float MAX_OTHER_CAT_CONFIDENCE = 0.15f;  // Max 15% for any other cat
    static constexpr int REQUIRED_CONSECUTIVE_DETECTIONS = 2;  // Require 2 consecutive detections
    static constexpr unsigned long COOLDOWN_PERIOD_MS = 60000; // 60 seconds cooldown
    static constexpr int MAX_ACTIVATION_DURATION_MS = 2000;    // 2 seconds max
    
    bool validateBootsDetection(const DetectionResult& result);
    bool checkOtherCatConfidence(const DetectionResult& result);
};

#endif
