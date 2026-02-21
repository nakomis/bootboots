#include "DeterrentController.h"
#include "PCF8574Manager.h"
#include "CaptureController.h"
#include "AWSAuth.h"
#include "SystemState.h"
#include <SDLogger.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>

DeterrentController::DeterrentController(PCF8574Manager* pcfManager, CaptureController* captureController, AWSAuth* awsAuth)
    : _pcfManager(pcfManager)
    , _captureController(captureController)
    , _awsAuth(awsAuth)
    , _isActive(false)
{
}

void DeterrentController::setUploadConfig(const char* apiHost) {
    _apiHost = apiHost;
}

bool DeterrentController::shouldActivate(const DetectionResult& result, float triggerThresh) const {
    if (!result.success) {
        return false;
    }

    // Check if Boots was detected with sufficient confidence
    if (result.detectedIndex == BOOTS_INDEX && result.confidence >= triggerThresh) {
        SDLogger::getInstance().infof("DeterrentController: Boots detected (%.1f%% >= %.1f%% threshold)",
            result.confidence * 100.0f, triggerThresh * 100.0f);
        return true;
    }

    // Log why we're not activating
    if (result.detectedIndex != BOOTS_INDEX) {
        SDLogger::getInstance().infof("DeterrentController: Detected %s (index %d), not Boots - no deterrent",
            result.detectedName.c_str(), result.detectedIndex);
    } else {
        SDLogger::getInstance().infof("DeterrentController: Boots confidence %.1f%% < %.1f%% threshold - no deterrent",
            result.confidence * 100.0f, triggerThresh * 100.0f);
    }

    return false;
}

void DeterrentController::activate(SystemState& state, bool dryRun) {
    if (_isActive) {
        SDLogger::getInstance().warnf("DeterrentController: Already active, ignoring activation request");
        return;
    }

    SDLogger::getInstance().criticalf("DeterrentController: *** ACTIVATING DETERRENT SEQUENCE (dryRun=%s) ***",
        dryRun ? "ON" : "OFF");
    _isActive = true;
    state.atomizerActivations++;

    // Step 1: LED strips ON
    if (_pcfManager) {
        _pcfManager->setLedStrip(true);
        SDLogger::getInstance().infof("DeterrentController: LED strips ON");
    }

    // Steps 2-7: Record video; atomizer fires after PRE_SPRAY_DELAY_MS into the recording
    VideoRecorder* videoRecorder = _captureController ? _captureController->getVideoRecorder() : nullptr;
    String videoFilename;

    if (videoRecorder) {
        const int totalDurationSec = (PRE_SPRAY_DELAY_MS + DETERRENT_DURATION_MS) / 1000 + 1; // ~10s
        SDLogger::getInstance().infof("DeterrentController: Recording %ds video at %d fps",
            totalDurationSec, VIDEO_FPS);

        VideoConfig config = VideoRecorder::getDefaultConfig();
        config.frameSize = FRAMESIZE_VGA;
        config.quality = 12;
        config.fps = VIDEO_FPS;
        config.durationSeconds = totalDurationSec;
        config.outputDir = "/videos";

        PCF8574Manager* pcf = _pcfManager;
        bool atomiserFired = false;
        bool atomiserStopped = false;

        VideoResult result = videoRecorder->recordWithProgress(config,
            [pcf, dryRun, &atomiserFired, &atomiserStopped](uint32_t currentFrame, uint32_t totalFrames, uint32_t elapsedMs) {
                static uint32_t lastSecond = 0;
                uint32_t currentSecond = elapsedMs / 1000;
                if (currentSecond != lastSecond) {
                    lastSecond = currentSecond;
                    SDLogger::getInstance().tracef("Deterrent recording: frame %d/%d (%.1fs)",
                        currentFrame, totalFrames, elapsedMs / 1000.0f);
                }

                // Fire atomizer after pre-spray delay
                if (!atomiserFired && elapsedMs >= PRE_SPRAY_DELAY_MS) {
                    if (!dryRun && pcf) {
                        pcf->setAtomizerState(true);
                        SDLogger::getInstance().infof("DeterrentController: Atomizer ON (T=%.1fs)", elapsedMs / 1000.0f);
                    } else {
                        SDLogger::getInstance().infof("DeterrentController: Dry-run — skipping atomizer");
                    }
                    atomiserFired = true;
                }

                // Stop atomizer after deterrent duration
                if (!atomiserStopped && elapsedMs >= PRE_SPRAY_DELAY_MS + DETERRENT_DURATION_MS) {
                    if (!dryRun && pcf) {
                        pcf->setAtomizerState(false);
                        SDLogger::getInstance().infof("DeterrentController: Atomizer OFF (T=%.1fs)", elapsedMs / 1000.0f);
                    }
                    atomiserStopped = true;
                }
            }
        );

        // Safety: ensure atomizer is off regardless of recording outcome
        if (_pcfManager) {
            _pcfManager->setAtomizerState(false);
        }

        if (result.success) {
            videoFilename = result.filename;
            SDLogger::getInstance().infof("DeterrentController: Video saved: %s (%d frames, %d bytes)",
                result.filename.c_str(), result.totalFrames, result.fileSize);
        } else {
            SDLogger::getInstance().errorf("DeterrentController: Video recording failed: %s",
                result.errorMessage.c_str());
        }
    } else {
        // No video recorder — still run the atomizer sequence with manual delays
        SDLogger::getInstance().warnf("DeterrentController: No video recorder — running atomizer-only sequence");
        delay(PRE_SPRAY_DELAY_MS);
        if (!dryRun && _pcfManager) {
            _pcfManager->setAtomizerState(true);
            SDLogger::getInstance().infof("DeterrentController: Atomizer ON");
        }
        delay(DETERRENT_DURATION_MS);
        if (_pcfManager) {
            _pcfManager->setAtomizerState(false);
            SDLogger::getInstance().infof("DeterrentController: Atomizer OFF");
        }
    }

    // Step 8: LED strips OFF
    if (_pcfManager) {
        _pcfManager->setLedStrip(false);
        SDLogger::getInstance().infof("DeterrentController: LED strips OFF");
    }

    // Step 9: Upload video
    if (!videoFilename.isEmpty()) {
        if (uploadVideo(videoFilename)) {
            SDLogger::getInstance().infof("DeterrentController: Video uploaded successfully");
        } else {
            SDLogger::getInstance().warnf("DeterrentController: Video upload failed (saved locally: %s)", videoFilename.c_str());
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

bool DeterrentController::uploadVideo(const String& filepath) {
    if (!_awsAuth || !_apiHost) {
        SDLogger::getInstance().warnf("DeterrentController: Upload not configured (awsAuth=%p, apiHost=%p)",
            _awsAuth, _apiHost);
        return false;
    }

    // Refresh credentials if needed
    if (!_awsAuth->areCredentialsValid()) {
        SDLogger::getInstance().infof("DeterrentController: Refreshing AWS credentials...");
        if (!_awsAuth->getCredentialsWithRoleAlias("BootBootsRoleAlias")) {
            SDLogger::getInstance().errorf("DeterrentController: Failed to refresh AWS credentials");
            return false;
        }
    }

    // Open the video file
    File videoFile = SD_MMC.open(filepath.c_str(), FILE_READ);
    if (!videoFile) {
        SDLogger::getInstance().errorf("DeterrentController: Failed to open video file: %s", filepath.c_str());
        return false;
    }

    size_t fileSize = videoFile.size();
    SDLogger::getInstance().infof("DeterrentController: Uploading video %s (%d bytes)", filepath.c_str(), fileSize);

    // Extract just the filename from the path (e.g., "/videos/video_xxx.avi" -> "video_xxx.avi")
    String filename = filepath;
    int lastSlash = filepath.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = filepath.substring(lastSlash + 1);
    }

    // Build the API path: /upload-video/{filename}
    String apiPath = "/upload-video/" + filename;

    // Create SigV4 headers for the PUT request
    // We need to read the file to compute the hash, then reset to beginning
    // For large files, we'll use a chunked approach, but for now read all
    uint8_t* videoBuffer = (uint8_t*)ps_malloc(fileSize);
    if (!videoBuffer) {
        SDLogger::getInstance().errorf("DeterrentController: Failed to allocate %d bytes for video upload", fileSize);
        videoFile.close();
        return false;
    }

    size_t bytesRead = videoFile.read(videoBuffer, fileSize);
    videoFile.close();

    if (bytesRead != fileSize) {
        SDLogger::getInstance().errorf("DeterrentController: Failed to read video file (read %d of %d bytes)",
            bytesRead, fileSize);
        free(videoBuffer);
        return false;
    }

    // Create SigV4 headers for PUT request with binary payload
    String contentType = "video/x-msvideo";
    SigV4Headers headers = _awsAuth->createSigV4HeadersForBinary("PUT", apiPath, _apiHost,
        videoBuffer, fileSize, contentType);

    if (!headers.isValid) {
        SDLogger::getInstance().errorf("DeterrentController: Failed to create SigV4 headers");
        free(videoBuffer);
        return false;
    }

    // Connect to API Gateway
    WiFiClientSecure client;
    client.setInsecure();  // For testing - in production, set proper CA cert
    client.setTimeout(60);  // 60 second timeout for large uploads

    SDLogger::getInstance().debugf("DeterrentController: Connecting to %s:443", _apiHost);
    if (!client.connect(_apiHost, 443)) {
        SDLogger::getInstance().errorf("DeterrentController: Connection to API failed");
        free(videoBuffer);
        return false;
    }

    // Send HTTP PUT request
    client.print("PUT ");
    client.print(apiPath);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(_apiHost);
    client.println("Connection: close");
    client.print("Content-Type: ");
    client.println(contentType);
    client.print("Content-Length: ");
    client.println(fileSize);
    client.print("X-Amz-Date: ");
    client.println(headers.date);
    client.print("X-Amz-Security-Token: ");
    client.println(headers.securityToken);
    client.print("Authorization: ");
    client.println(headers.authorization);
    client.print("X-Amz-Content-Sha256: ");
    client.println(headers.payloadHash);
    client.println();

    // Send video data in chunks
    size_t bytesRemaining = fileSize;
    uint8_t* dataPtr = videoBuffer;
    size_t chunkSize = 4096;  // Larger chunks for video

    SDLogger::getInstance().debugf("DeterrentController: Sending %d bytes", fileSize);

    while (bytesRemaining > 0) {
        size_t chunk = (bytesRemaining < chunkSize) ? bytesRemaining : chunkSize;
        size_t bytesWritten = client.write(dataPtr, chunk);

        if (bytesWritten != chunk) {
            SDLogger::getInstance().errorf("DeterrentController: Error writing data (wrote %d of %d)",
                bytesWritten, chunk);
            free(videoBuffer);
            client.stop();
            return false;
        }

        dataPtr += bytesWritten;
        bytesRemaining -= bytesWritten;
        yield();  // Prevent WDT reset
    }

    free(videoBuffer);

    SDLogger::getInstance().debugf("DeterrentController: Data sent, waiting for response");

    // Read response with timeout
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 30000) {
            SDLogger::getInstance().errorf("DeterrentController: Response timeout");
            client.stop();
            return false;
        }
        delay(10);
    }

    // Read HTTP status line
    String statusLine = client.readStringUntil('\n');
    SDLogger::getInstance().infof("DeterrentController: %s", statusLine.c_str());

    // Extract status code
    int statusCode = 0;
    int spaceIndex = statusLine.indexOf(' ');
    if (spaceIndex > 0) {
        statusCode = statusLine.substring(spaceIndex + 1, spaceIndex + 4).toInt();
    }

    // Read and discard remaining response
    while (client.available()) {
        client.read();
    }
    client.stop();

    if (statusCode == 200) {
        SDLogger::getInstance().infof("DeterrentController: Video upload successful (HTTP %d)", statusCode);
        return true;
    } else {
        SDLogger::getInstance().errorf("DeterrentController: Video upload failed (HTTP %d)", statusCode);
        return false;
    }
}
