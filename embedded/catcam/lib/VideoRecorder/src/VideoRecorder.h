#ifndef CATCAM_VIDEO_RECORDER_H
#define CATCAM_VIDEO_RECORDER_H

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_camera.h>
#include <functional>

// Video recording configuration
struct VideoConfig {
    framesize_t frameSize;      // Camera frame size (default: VGA 640x480)
    uint8_t quality;            // JPEG quality 0-63 (lower = better, default: 12)
    uint8_t fps;                // Target frames per second (default: 10)
    uint16_t durationSeconds;   // Recording duration in seconds (default: 10)
    const char* outputDir;      // Output directory (default: "/videos")
};

// Recording result
struct VideoResult {
    bool success;
    String filename;
    uint32_t totalFrames;
    uint32_t fileSize;
    uint32_t durationMs;
    String errorMessage;
};

// Callback for progress updates during recording (std::function supports capturing lambdas)
using VideoProgressCallback = std::function<void(uint32_t currentFrame, uint32_t totalFrames, uint32_t elapsedMs)>;

class VideoRecorder {
public:
    VideoRecorder();

    /**
     * Initialize the video recorder
     * Must be called before recording
     * Note: Camera must already be initialized by the Camera class
     */
    bool init();

    /**
     * Record a video with default settings (10 seconds, VGA, 10fps)
     * @return VideoResult with recording details
     */
    VideoResult record();

    /**
     * Record a video with custom configuration
     * @param config Video recording configuration
     * @return VideoResult with recording details
     */
    VideoResult record(const VideoConfig& config);

    /**
     * Record a video with progress callback
     * @param config Video recording configuration
     * @param callback Progress callback function
     * @return VideoResult with recording details
     */
    VideoResult recordWithProgress(const VideoConfig& config, VideoProgressCallback callback);

    /**
     * Stop recording early (call from another task/ISR)
     */
    void stopRecording();

    /**
     * Check if currently recording
     */
    bool isRecording() const { return _isRecording; }

    /**
     * Get default configuration
     */
    static VideoConfig getDefaultConfig();

private:
    bool _initialized;
    volatile bool _isRecording;
    volatile bool _stopRequested;

    // Original camera settings to restore after recording
    framesize_t _originalFrameSize;
    int _originalQuality;

    // AVI file writing helpers
    bool writeAviHeader(File& file, uint16_t width, uint16_t height, uint8_t fps, uint32_t totalFrames);
    bool writeFrameToAvi(File& file, camera_fb_t* frame, uint32_t frameIndex);
    bool finalizeAvi(File& file, uint32_t totalFrames, uint32_t moviSize);

    // Frame index for AVI
    struct FrameIndex {
        uint32_t offset;
        uint32_t size;
    };
    static const uint32_t MAX_FRAMES = 300;  // Max 30 seconds at 10fps
    FrameIndex _frameIndex[MAX_FRAMES];

    // Utility
    String generateFilename(const char* outputDir);
    void saveOriginalCameraSettings();
    void restoreOriginalCameraSettings();
    bool setCameraForVideo(const VideoConfig& config);
};

#endif
