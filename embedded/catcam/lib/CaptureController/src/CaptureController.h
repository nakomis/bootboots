#pragma once

#include <Arduino.h>
#include <functional>
#include "Camera.h"
#include "VideoRecorder.h"
#include "LedController.h"
#include "ImageStorage.h"
#include "AWSAuth.h"

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

private:
    Camera* _camera;
    VideoRecorder* _videoRecorder;
    LedController* _ledController;
    ImageStorage* _imageStorage;
    AWSAuth* _awsAuth;

    bool _initialized = false;

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
};
