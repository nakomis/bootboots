#include "DeterrentController.h"
#include "PCF8574Manager.h"
#include "CaptureController.h"
#include "SystemState.h"
#include <SDLogger.h>

DeterrentController::DeterrentController(PCF8574Manager* pcfManager, CaptureController* captureController)
    : _pcfManager(pcfManager)
    , _captureController(captureController)
    , _isActive(false)
{
}

bool DeterrentController::shouldActivate(const DetectionResult& result) const {
    if (!result.success) {
        return false;
    }

    // Check if Boots was detected with sufficient confidence
    if (result.detectedIndex == BOOTS_INDEX && result.confidence >= CONFIDENCE_THRESHOLD) {
        SDLogger::getInstance().infof("DeterrentController: Boots detected (%.1f%% >= %.1f%% threshold)",
            result.confidence * 100.0f, CONFIDENCE_THRESHOLD * 100.0f);
        return true;
    }

    // Log why we're not activating
    if (result.detectedIndex != BOOTS_INDEX) {
        SDLogger::getInstance().infof("DeterrentController: Detected %s (index %d), not Boots - no deterrent",
            result.detectedName.c_str(), result.detectedIndex);
    } else {
        SDLogger::getInstance().infof("DeterrentController: Boots confidence %.1f%% < %.1f%% threshold - no deterrent",
            result.confidence * 100.0f, CONFIDENCE_THRESHOLD * 100.0f);
    }

    return false;
}

void DeterrentController::activate(SystemState& state) {
    if (_isActive) {
        SDLogger::getInstance().warnf("DeterrentController: Already active, ignoring activation request");
        return;
    }

    SDLogger::getInstance().criticalf("DeterrentController: *** ACTIVATING DETERRENT SEQUENCE ***");
    _isActive = true;

    // Track activation in system state
    state.atomizerActivations++;

    // Start mister
    if (_pcfManager) {
        if (_pcfManager->setAtomizerState(true)) {
            SDLogger::getInstance().infof("DeterrentController: Mister ACTIVATED");
        } else {
            SDLogger::getInstance().errorf("DeterrentController: Failed to activate mister");
        }
    }

    // Record video for 8 seconds (this is blocking)
    if (_captureController) {
        int durationSeconds = DETERRENT_DURATION_MS / 1000;
        SDLogger::getInstance().infof("DeterrentController: Recording %d second video at %d fps",
            durationSeconds, VIDEO_FPS);

        // Use the captureController's video recording without LED countdown
        // We need to use the VideoRecorder directly for no-countdown recording
        VideoRecorder* videoRecorder = _captureController->getVideoRecorder();
        if (videoRecorder) {
            VideoConfig config = VideoRecorder::getDefaultConfig();
            config.frameSize = FRAMESIZE_VGA;
            config.quality = 12;
            config.fps = VIDEO_FPS;
            config.durationSeconds = durationSeconds;
            config.outputDir = "/videos";

            VideoResult result = videoRecorder->recordWithProgress(config,
                [](uint32_t currentFrame, uint32_t totalFrames, uint32_t elapsedMs) {
                    static uint32_t lastSecond = 0;
                    uint32_t currentSecond = elapsedMs / 1000;
                    if (currentSecond != lastSecond) {
                        lastSecond = currentSecond;
                        SDLogger::getInstance().debugf("Deterrent recording: frame %d/%d (%.1fs)",
                            currentFrame, totalFrames, elapsedMs / 1000.0f);
                    }
                }
            );

            if (result.success) {
                SDLogger::getInstance().infof("DeterrentController: Video saved: %s (%d frames, %d bytes)",
                    result.filename.c_str(), result.totalFrames, result.fileSize);
            } else {
                SDLogger::getInstance().errorf("DeterrentController: Video recording failed: %s",
                    result.errorMessage.c_str());
            }
        }
    }

    // Stop mister
    if (_pcfManager) {
        if (_pcfManager->setAtomizerState(false)) {
            SDLogger::getInstance().infof("DeterrentController: Mister DEACTIVATED");
        } else {
            SDLogger::getInstance().errorf("DeterrentController: Failed to deactivate mister");
        }
    }

    _isActive = false;
    SDLogger::getInstance().criticalf("DeterrentController: *** DETERRENT SEQUENCE COMPLETE ***");
}

void DeterrentController::emergencyStop() {
    SDLogger::getInstance().criticalf("DeterrentController: *** EMERGENCY STOP ***");

    if (_pcfManager) {
        _pcfManager->setAtomizerState(false);
    }

    _isActive = false;
}
