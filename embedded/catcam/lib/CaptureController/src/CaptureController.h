#pragma once

#include <Arduino.h>
#include <functional>
#include "Camera.h"
#include "VideoRecorder.h"
#include "LedController.h"
#include "ImageStorage.h"
#include "AWSAuth.h"

/**
 * DetectionResult - Result from capture and inference
 */
struct DetectionResult {
    bool success = false;           // True if capture and inference succeeded
    String detectedName;            // Name of detected cat (e.g., "Boots", "Chi", "NoCat")
    int detectedIndex = -1;         // Index in model output (0=Boots, 1=Chi, etc.)
    float confidence = 0.0f;        // Confidence score (0.0 to 1.0)
    String filename;                // Filename of captured image
    String rawResponse;             // Raw JSON response from inference API
};

/**
 * CaptureController - Orchestrates photo and video capture
 *
 * Coordinates Camera, VideoRecorder, LedController, ImageStorage, and AWSAuth
 * to provide high-level capture operations with visual feedback.
 */
class CaptureController {
public:
    // Callback types for external integration
    using CancelCheckCallback = std::function<bool()>;
    using LoopCallback = std::function<void()>;

    /**
     * Constructor - takes pointers to required components
     */
    CaptureController(Camera* camera, VideoRecorder* videoRecorder,
                      LedController* ledController, ImageStorage* imageStorage,
                      AWSAuth* awsAuth);

    /**
     * Initialize the capture controller
     * Runs LED test sequence and initializes camera/video recorder
     * @return true if initialization successful
     */
    bool init();

    /**
     * Check if the controller is initialized and ready
     */
    bool isReady() const { return _initialized; }

    /**
     * Set callbacks for cancel checking and background task handling
     * These are called during LED countdown animations
     */
    void setCallbacks(CancelCheckCallback cancelCheck, LoopCallback loopCallback);

    /**
     * Configure AWS endpoint for photo uploads
     */
    void setAWSConfig(const char* roleAlias, const char* apiHost, const char* apiPath);

    /**
     * Capture a photo with LED countdown, upload to AWS, and save locally
     * @return Filename of captured image (with .jpg extension), or empty string on failure
     */
    String capturePhoto();

    /**
     * Capture photo and run inference without LED countdown (for PIR-triggered detection)
     * Quick response path - captures photo, uploads to AWS, returns structured result
     * @return DetectionResult with inference results
     */
    DetectionResult captureAndDetect();

    /**
     * Record a video with LED countdown
     * @param durationSeconds Recording duration (default 10)
     * @param fps Frames per second (default 10)
     * @return true if recording succeeded
     */
    bool recordVideo(int durationSeconds = 10, int fps = 10);

    /**
     * Get the Camera instance
     */
    Camera* getCamera() { return _camera; }

    /**
     * Get the VideoRecorder instance
     */
    VideoRecorder* getVideoRecorder() { return _videoRecorder; }

    /**
     * Set training mode
     * In training mode, photos are uploaded without inference/deterrent
     */
    void setTrainingMode(bool enabled) { _trainingMode = enabled; }

    /**
     * Check if training mode is enabled
     */
    bool isTrainingMode() const { return _trainingMode; }

    /**
     * Capture photo in training mode - uploads to training/ prefix, skips inference
     * @return Filename of captured image (with .jpg extension), or empty string on failure
     */
    String captureTrainingPhoto();

private:
    Camera* _camera;
    VideoRecorder* _videoRecorder;
    LedController* _ledController;
    ImageStorage* _imageStorage;
    AWSAuth* _awsAuth;

    bool _initialized = false;
    bool _trainingMode = false;

    // Callbacks
    CancelCheckCallback _cancelCheck = nullptr;
    LoopCallback _loopCallback = nullptr;

    // AWS configuration
    const char* _roleAlias = nullptr;
    const char* _apiHost = nullptr;
    const char* _apiPath = nullptr;

    // Helper methods
    void runCountdown();
    void parseAndLogInferenceResponse(const String& response);
    DetectionResult parseInferenceResponse(const String& response, const String& filename);
};
