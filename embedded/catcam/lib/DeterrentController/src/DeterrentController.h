#pragma once

#include <Arduino.h>

// Forward declarations
class PCF8574Manager;
class CaptureController;
class AWSAuth;
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
    static constexpr unsigned long DETERRENT_DURATION_MS = 8000;  // Atomizer on duration (ms)
    static constexpr unsigned long PRE_SPRAY_DELAY_MS = 1000;     // LEDs+video before atomizer fires
    static constexpr int VIDEO_FPS = 10;
    static constexpr int BOOTS_INDEX = 0;  // Boots is index 0 in binary model output [0]=Boots, [1]=NotBoots

    /**
     * Constructor
     * @param pcfManager Pointer to PCF8574Manager for mister control
     * @param captureController Pointer to CaptureController for video recording
     * @param awsAuth Pointer to AWSAuth for video upload authentication
     */
    DeterrentController(PCF8574Manager* pcfManager, CaptureController* captureController, AWSAuth* awsAuth);

    /**
     * Configure the video upload API endpoint
     * @param apiHost API Gateway host (e.g., "api.bootboots.sandbox.nakomis.com")
     */
    void setUploadConfig(const char* apiHost);

    /**
     * Check if deterrent should be activated based on detection result
     * @param result Detection result from captureAndDetect()
     * @param triggerThresh Boots confidence required to fire (0.0-1.0)
     * @return true if Boots detected with >= triggerThresh confidence
     */
    bool shouldActivate(const DetectionResult& result, float triggerThresh) const;

    /**
     * Activate the deterrent sequence (BLOCKING ~10s):
     * 1. LED strips ON
     * 2. Video recording starts
     * 3. 1s delay
     * 4. Atomizer ON (unless dryRun)
     * 5. 8s delay
     * 6. Atomizer OFF
     * 7. Video recording stops
     * 8. LED strips OFF
     * 9. Video uploaded to cloud
     * @param state System state for tracking activations
     * @param dryRun If true, skip atomizer but run all other steps
     */
    void activate(SystemState& state, bool dryRun = false);

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
    AWSAuth* _awsAuth;
    bool _isActive;

    // Upload configuration
    const char* _apiHost = nullptr;

    /**
     * Upload a video file to the cloud
     * @param filepath Full path to the video file on SD card
     * @return true if upload succeeded
     */
    bool uploadVideo(const String& filepath);
};
