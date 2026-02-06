#include "CaptureController.h"
#include <SDLogger.h>
#include <ArduinoJson.h>
#include "CatCamHttpClient.h"

CaptureController::CaptureController(Camera* camera, VideoRecorder* videoRecorder,
                                     LedController* ledController, ImageStorage* imageStorage,
                                     AWSAuth* awsAuth)
    : _camera(camera), _videoRecorder(videoRecorder), _ledController(ledController),
      _imageStorage(imageStorage), _awsAuth(awsAuth) {
}

bool CaptureController::init(const CameraSettings& settings) {
    SDLogger::getInstance().infof("=== Initializing CaptureController ===");

    // Initialize LED controller and run test sequence
    if (_ledController) {
        _ledController->init(100);  // Default brightness 100
        _ledController->runTestSequence(3, 100);
    }

    // Initialize camera with settings (frame size, quality, buffer count)
    if (_camera) {
        _camera->init(settings);
        delay(500);  // Give camera time to stabilize
        if (!_camera->isReady()) {
            SDLogger::getInstance().errorf("Camera initialization failed - photo capture will be unavailable");
        }
    } else {
        SDLogger::getInstance().errorf("Camera not provided");
        return false;
    }

    // Initialize video recorder
    if (_videoRecorder) {
        if (_videoRecorder->init()) {
            SDLogger::getInstance().infof("Video Recorder initialized successfully");
        } else {
            SDLogger::getInstance().warnf("Video Recorder initialization failed");
        }
    }

    _initialized = true;
    SDLogger::getInstance().infof("=== CaptureController Ready ===");
    return true;
}

void CaptureController::setCallbacks(CancelCheckCallback cancelCheck, LoopCallback loopCallback) {
    _cancelCheck = cancelCheck;
    _loopCallback = loopCallback;
}

void CaptureController::setFlashCallback(FlashCallback flashCallback) {
    _flashCallback = flashCallback;
}

void CaptureController::setAWSConfig(const char* roleAlias, const char* apiHost, const char* apiPath) {
    _roleAlias = roleAlias;
    _apiHost = apiHost;
    _apiPath = apiPath;
}

void CaptureController::runCountdown() {
    if (!_ledController) return;

    // Step 1: Flash red LED (starts slow, gets faster)
    SDLogger::getInstance().debugf("Red LED countdown...");
    _ledController->flashAccelerating(255, 0, 0, 500, 250, 2500, _cancelCheck, _loopCallback);

    // Step 2: Flash blue LED (starts slow, gets faster)
    SDLogger::getInstance().debugf("Blue LED countdown...");
    _ledController->flashAccelerating(0, 0, 255, 250, 50, 2500, _cancelCheck, _loopCallback);
}

String CaptureController::capturePhoto() {
    if (!_camera || !_camera->isReady()) {
        SDLogger::getInstance().errorf("Camera not available - cannot capture photo");
        return "";
    }

    SDLogger::getInstance().infof("=== Capturing Photo ===");

    // Run LED countdown
    runCountdown();

    // Set bright WHITE for photo capture
    if (_ledController) {
        _ledController->setBrightness(255);
        _ledController->setColor(255, 255, 255);
    }

    // Turn on external flash for capture
    if (_flashCallback) _flashCallback(true);
    
    // Wait a short while for the LEDs to warm up
    SDLogger::getInstance().infof("capturePhoto waiting for %d millis to allow LEDs to warm", _camera->getLedDelayMillis());
    delay(_camera->getLedDelayMillis());

    // Capture image
    NamedImage* image = _camera->getImage();

    // Turn off external flash after capture
    if (_flashCallback) _flashCallback(false);

    if (!image || !image->image || image->size == 0) {
        SDLogger::getInstance().errorf("Failed to capture image");
        if (_ledController) _ledController->off();
        return "";
    }

    // Generate timestamp-based filename
    String basename = _imageStorage ? _imageStorage->generateFilename() : String(millis());

    // Set moderate brightness green to indicate capture success
    if (_ledController) {
        _ledController->setBrightness(50);
        _ledController->setColor(0, 255, 0);
    }

    SDLogger::getInstance().infof("Captured image: %s (%d bytes)", basename.c_str(), image->size);

    // Save image to SD card
    if (_imageStorage) {
        _imageStorage->saveImage(basename, image);
    }

    // Upload to AWS if configured
    String response;
    if (_awsAuth && _roleAlias && _apiHost && _apiPath) {
        // Get AWS credentials (refresh if needed)
        if (!_awsAuth->areCredentialsValid()) {
            SDLogger::getInstance().infof("Refreshing AWS credentials...");
            if (!_awsAuth->getCredentialsWithRoleAlias(_roleAlias)) {
                SDLogger::getInstance().errorf("Failed to get AWS credentials");
                _camera->releaseImageBuffer(image);
                if (_ledController) _ledController->off();
                return "";
            }
        }

        // Post image to inference endpoint
        CatCamHttpClient httpClient;
        response = httpClient.postImage(image, _apiHost, _apiPath, _awsAuth);

        // Save server response to SD card
        if (_imageStorage) {
            _imageStorage->saveResponse(basename, response);
        }

        // Parse and log inference results
        parseAndLogInferenceResponse(response);
    }

    // Release image buffer
    _camera->releaseImageBuffer(image);

    // Clean up old images
    if (_imageStorage) {
        _imageStorage->cleanupOldImages();
    }

    // Turn LED off
    if (_ledController) {
        _ledController->off();
    }

    SDLogger::getInstance().infof("=== Photo Capture Complete ===");

    return basename + ".jpg";
}

bool CaptureController::recordVideo(int durationSeconds, int fps) {
    if (!_videoRecorder) {
        SDLogger::getInstance().errorf("Video recorder not initialized");
        return false;
    }

    if (_videoRecorder->isRecording()) {
        SDLogger::getInstance().warnf("Video recording already in progress");
        return false;
    }

    SDLogger::getInstance().infof("=== Starting Video Recording ===");

    // Run LED countdown
    runCountdown();

    // Set WHITE LED on during recording
    if (_ledController) {
        _ledController->setBrightness(255);
        _ledController->setColor(255, 255, 255);
    }

    // Configure recording
    VideoConfig config = VideoRecorder::getDefaultConfig();
    config.frameSize = FRAMESIZE_VGA;
    config.quality = 12;
    config.fps = fps;
    config.durationSeconds = durationSeconds;
    config.outputDir = "/videos";

    SDLogger::getInstance().infof("Recording %d seconds of video at %d fps...",
        config.durationSeconds, config.fps);

    // Record with progress callback
    VideoResult result = _videoRecorder->recordWithProgress(config,
        [](uint32_t currentFrame, uint32_t totalFrames, uint32_t elapsedMs) {
            static uint32_t lastSecond = 0;
            uint32_t currentSecond = elapsedMs / 1000;
            if (currentSecond != lastSecond) {
                lastSecond = currentSecond;
                SDLogger::getInstance().debugf("Recording: frame %d/%d (%.1fs)",
                    currentFrame, totalFrames, elapsedMs / 1000.0f);
            }
        }
    );

    // Turn LED off and restore normal brightness
    if (_ledController) {
        _ledController->off();
        _ledController->setBrightness(100);

        if (result.success) {
            _ledController->flashSuccess();
        } else {
            _ledController->flashError();
        }
    }

    if (result.success) {
        SDLogger::getInstance().infof("=== Video Recording Complete ===");
        SDLogger::getInstance().infof("Saved: %s (%d frames, %d bytes, %d ms)",
            result.filename.c_str(), result.totalFrames, result.fileSize, result.durationMs);
    } else {
        SDLogger::getInstance().errorf("=== Video Recording Failed ===");
        SDLogger::getInstance().errorf("Error: %s", result.errorMessage.c_str());
    }

    return result.success;
}

void CaptureController::parseAndLogInferenceResponse(const String& response) {
    DynamicJsonDocument doc(2048);
    DeserializationError jsonError = deserializeJson(doc, response);

    // Cat names matching model output indices
    const char* CAT_NAMES[] = { "Boots", "Chi", "Kappa", "Mu", "Tau", "NoCat" };

    if (jsonError) {
        SDLogger::getInstance().warnf("Failed to parse response JSON: %s", jsonError.c_str());
        SDLogger::getInstance().infof("Raw response: %s", response.c_str());
    }
    else if (doc["success"] == true) {
        // Get winner from mostLikelyCat object at root level
        JsonObject mostLikelyCat = doc["mostLikelyCat"];
        const char* winnerName = mostLikelyCat["name"] | "Unknown";
        float winnerConfidence = mostLikelyCat["confidence"] | 0.0f;

        // Build results string from probabilities array in data
        String resultLog = "";
        JsonObject data = doc["data"];
        if (data.containsKey("probabilities") && data["probabilities"].is<JsonArray>()) {
            JsonArray probabilities = data["probabilities"];
            for (size_t i = 0; i < probabilities.size() && i < 6; i++) {
                float score = probabilities[i].as<float>();
                char scoreStr[32];
                snprintf(scoreStr, sizeof(scoreStr), "%s=%.1f%% ", CAT_NAMES[i], score * 100.0);
                resultLog += scoreStr;
            }
        }

        SDLogger::getInstance().infof("%s | Winner: %s (%.1f%%)",
            resultLog.c_str(), winnerName, winnerConfidence * 100.0);
    }
    else {
        SDLogger::getInstance().warnf("Unexpected response format: %s", response.c_str());
    }
}

DetectionResult CaptureController::parseInferenceResponse(const String& response, const String& filename) {
    DetectionResult result;
    result.filename = filename;
    result.rawResponse = response;

    DynamicJsonDocument doc(2048);
    DeserializationError jsonError = deserializeJson(doc, response);

    if (jsonError) {
        SDLogger::getInstance().warnf("Failed to parse inference response: %s", jsonError.c_str());
        return result;
    }

    if (doc["success"] != true) {
        SDLogger::getInstance().warnf("Inference API returned success=false");
        return result;
    }

    // Get winner from mostLikelyCat object at root level
    JsonObject mostLikelyCat = doc["mostLikelyCat"];
    if (mostLikelyCat.isNull()) {
        SDLogger::getInstance().warnf("No mostLikelyCat in response");
        return result;
    }

    result.success = true;
    result.detectedName = mostLikelyCat["name"] | "Unknown";
    result.confidence = mostLikelyCat["confidence"] | 0.0f;
    result.detectedIndex = mostLikelyCat["index"] | -1;

    return result;
}

String CaptureController::captureTrainingPhoto() {
    if (!_camera || !_camera->isReady()) {
        SDLogger::getInstance().errorf("Camera not available - cannot capture photo");
        return "";
    }

    SDLogger::getInstance().infof("=== Capturing Training Photo ===");

    // No LED countdown for training captures (similar to quick PIR captures)

    // Turn on external flash for capture
    if (_flashCallback) _flashCallback(true);
    
    // Wait a short while for the LEDs to warm up
    SDLogger::getInstance().infof("captureTrainingPhoto Waiting for %d millis to allow LEDs to warm", _camera->getLedDelayMillis());
    delay(_camera->getLedDelayMillis());

    // Capture image
    NamedImage* image = _camera->getImage();

    // Turn off external flash after capture
    if (_flashCallback) _flashCallback(false);

    if (!image || !image->image || image->size == 0) {
        SDLogger::getInstance().errorf("Failed to capture image");
        return "";
    }

    // Generate timestamp-based filename
    String basename = _imageStorage ? _imageStorage->generateFilename() : String(millis());

    SDLogger::getInstance().infof("Captured training image: %s (%d bytes)", basename.c_str(), image->size);

    // Save image to SD card
    if (_imageStorage) {
        _imageStorage->saveImage(basename, image);
    }

    // Upload to AWS with training mode flag
    String response;
    if (_awsAuth && _roleAlias && _apiHost && _apiPath) {
        // Get AWS credentials (refresh if needed)
        if (!_awsAuth->areCredentialsValid()) {
            SDLogger::getInstance().infof("Refreshing AWS credentials...");
            if (!_awsAuth->getCredentialsWithRoleAlias(_roleAlias)) {
                SDLogger::getInstance().errorf("Failed to get AWS credentials");
                _camera->releaseImageBuffer(image);
                return "";
            }
        }

        // Post image to inference endpoint with training mode flag
        CatCamHttpClient httpClient;
        response = httpClient.postImage(image, _apiHost, _apiPath, _awsAuth, true);  // trainingMode=true

        // Save server response to SD card
        if (_imageStorage) {
            _imageStorage->saveResponse(basename, response);
        }

        // Log training response (no inference to parse)
        SDLogger::getInstance().infof("Training upload response: %s", response.c_str());
    } else {
        SDLogger::getInstance().warnf("AWS not configured - cannot upload training photo");
    }

    // Release image buffer
    _camera->releaseImageBuffer(image);

    // Clean up old images
    if (_imageStorage) {
        _imageStorage->cleanupOldImages();
    }

    SDLogger::getInstance().infof("=== Training Photo Capture Complete ===");

    return basename + ".jpg";
}

DetectionResult CaptureController::captureAndDetect() {
    DetectionResult result;

    if (!_camera || !_camera->isReady()) {
        SDLogger::getInstance().errorf("Camera not available - cannot capture photo");
        return result;
    }

    SDLogger::getInstance().infof("=== Quick Capture for Detection ===");

    // No LED countdown for quick PIR-triggered capture

    // Turn on external flash for capture
    if (_flashCallback) _flashCallback(true);

    // Wait a short while for the LEDs to warm up
    SDLogger::getInstance().infof("captureAndDetect Waiting for %d millis to allow LEDs to warm", _camera->getLedDelayMillis());
    delay(_camera->getLedDelayMillis());

    // Capture image
    NamedImage* image = _camera->getImage();

    // Turn off external flash after capture
    if (_flashCallback) _flashCallback(false);

    if (!image || !image->image || image->size == 0) {
        SDLogger::getInstance().errorf("Failed to capture image");
        return result;
    }

    // Generate timestamp-based filename
    String basename = _imageStorage ? _imageStorage->generateFilename() : String(millis());

    SDLogger::getInstance().infof("Captured image: %s (%d bytes)", basename.c_str(), image->size);

    // Save image to SD card
    if (_imageStorage) {
        _imageStorage->saveImage(basename, image);
    }

    // Upload to AWS and get inference result
    String response;
    if (_awsAuth && _roleAlias && _apiHost && _apiPath) {
        // Get AWS credentials (refresh if needed)
        if (!_awsAuth->areCredentialsValid()) {
            SDLogger::getInstance().infof("Refreshing AWS credentials...");
            if (!_awsAuth->getCredentialsWithRoleAlias(_roleAlias)) {
                SDLogger::getInstance().errorf("Failed to get AWS credentials");
                _camera->releaseImageBuffer(image);
                return result;
            }
        }

        // Post image to inference endpoint
        CatCamHttpClient httpClient;
        response = httpClient.postImage(image, _apiHost, _apiPath, _awsAuth);

        // Save server response to SD card
        if (_imageStorage) {
            _imageStorage->saveResponse(basename, response);
        }

        // Parse response into DetectionResult
        result = parseInferenceResponse(response, basename + ".jpg");

        // Also log the inference results
        parseAndLogInferenceResponse(response);
    } else {
        SDLogger::getInstance().warnf("AWS not configured - cannot run inference");
    }

    // Release image buffer
    _camera->releaseImageBuffer(image);

    // Clean up old images
    if (_imageStorage) {
        _imageStorage->cleanupOldImages();
    }

    SDLogger::getInstance().infof("=== Detection Complete ===");

    return result;
}
