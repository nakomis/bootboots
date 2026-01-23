#pragma once

#include <Arduino.h>

// Forward declarations
class PCF8574Manager;
class CaptureController;
struct SystemState;
struct DetectionResult;

/**
 * DeterrentController - Orchestrates mister + video recording deterrent sequence
 *
 * When Boots is detected with sufficient confidence:
 * 1. Start 8-second video recording
 * 2. Activate mister for 8 seconds
 * 3. Stop both after 8 seconds
 */
class DeterrentController {
public:
    // Configuration constants
    static constexpr float CONFIDENCE_THRESHOLD = 0.80f;  // 80% confidence required
    static constexpr unsigned long DETERRENT_DURATION_MS = 8000;  // 8 seconds
    static constexpr int VIDEO_FPS = 10;
    static constexpr int BOOTS_INDEX = 0;  // Boots is index 0 in model output

    /**
     * Constructor
     * @param pcfManager Pointer to PCF8574Manager for mister control
     * @param captureController Pointer to CaptureController for video recording
     */
    DeterrentController(PCF8574Manager* pcfManager, CaptureController* captureController);

    /**
     * Check if deterrent should be activated based on detection result
     * @param result Detection result from captureAndDetect()
     * @return true if Boots detected with >= 80% confidence
     */
    bool shouldActivate(const DetectionResult& result) const;

    /**
     * Activate the deterrent sequence (BLOCKING)
     * - Starts mister via PCF8574
     * - Records 8-second video
     * - Stops mister after recording completes
     * @param state System state for tracking activations
     */
    void activate(SystemState& state);

    /**
     * Emergency stop - immediately deactivate mister
     */
    void emergencyStop();

    /**
     * Check if deterrent is currently active
     */
    bool isActive() const { return _isActive; }

private:
    PCF8574Manager* _pcfManager;
    CaptureController* _captureController;
    bool _isActive;
};
