#include "VideoRecorder.h"
#include "../../SDLogger/src/SDLogger.h"

// AVI uses little-endian format
static void writeU32(File& file, uint32_t value) {
    uint8_t buf[4];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
    file.write(buf, 4);
}

static void writeU16(File& file, uint16_t value) {
    uint8_t buf[2];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    file.write(buf, 2);
}

static void writeFourCC(File& file, const char* fourcc) {
    file.write((const uint8_t*)fourcc, 4);
}

VideoRecorder::VideoRecorder()
    : _initialized(false)
    , _isRecording(false)
    , _stopRequested(false)
    , _originalFrameSize(FRAMESIZE_UXGA)
    , _originalQuality(10) {
}

bool VideoRecorder::init() {
    // SD card should already be initialized by SDLogger
    _initialized = true;
    SDLogger::getInstance().infof("VideoRecorder initialized");
    return true;
}

VideoConfig VideoRecorder::getDefaultConfig() {
    VideoConfig config;
    config.frameSize = FRAMESIZE_VGA;      // 640x480
    config.quality = 12;                    // Good quality JPEG
    config.fps = 10;                        // 10 frames per second
    config.durationSeconds = 10;            // 10 second video
    config.outputDir = "/videos";
    return config;
}

VideoResult VideoRecorder::record() {
    return record(getDefaultConfig());
}

VideoResult VideoRecorder::record(const VideoConfig& config) {
    return recordWithProgress(config, nullptr);
}

VideoResult VideoRecorder::recordWithProgress(const VideoConfig& config, VideoProgressCallback callback) {
    VideoResult result;
    result.success = false;
    result.totalFrames = 0;
    result.fileSize = 0;
    result.durationMs = 0;

    if (!_initialized) {
        result.errorMessage = "VideoRecorder not initialized";
        SDLogger::getInstance().errorf("VideoRecorder: %s", result.errorMessage.c_str());
        return result;
    }

    if (_isRecording) {
        result.errorMessage = "Recording already in progress";
        SDLogger::getInstance().errorf("VideoRecorder: %s", result.errorMessage.c_str());
        return result;
    }

    _isRecording = true;
    _stopRequested = false;

    // Save current camera settings
    saveOriginalCameraSettings();

    // Configure camera for video
    if (!setCameraForVideo(config)) {
        result.errorMessage = "Failed to configure camera for video";
        SDLogger::getInstance().errorf("VideoRecorder: %s", result.errorMessage.c_str());
        restoreOriginalCameraSettings();
        _isRecording = false;
        return result;
    }

    // Create output directory if needed
    if (!SD_MMC.exists(config.outputDir)) {
        SD_MMC.mkdir(config.outputDir);
        SDLogger::getInstance().infof("Created video directory: %s", config.outputDir);
    }

    // Generate filename
    result.filename = generateFilename(config.outputDir);
    SDLogger::getInstance().infof("Recording video to: %s", result.filename.c_str());

    // Get frame dimensions
    uint16_t width = 0, height = 0;
    switch (config.frameSize) {
        case FRAMESIZE_QVGA:  width = 320;  height = 240;  break;
        case FRAMESIZE_CIF:   width = 400;  height = 296;  break;
        case FRAMESIZE_VGA:   width = 640;  height = 480;  break;
        case FRAMESIZE_SVGA:  width = 800;  height = 600;  break;
        case FRAMESIZE_XGA:   width = 1024; height = 768;  break;
        case FRAMESIZE_HD:    width = 1280; height = 720;  break;
        default:              width = 640;  height = 480;  break;
    }

    // Calculate expected frames
    uint32_t targetFrames = config.fps * config.durationSeconds;
    if (targetFrames > MAX_FRAMES) {
        targetFrames = MAX_FRAMES;
    }
    uint32_t frameIntervalMs = 1000 / config.fps;

    // Open file for writing
    File aviFile = SD_MMC.open(result.filename.c_str(), FILE_WRITE);
    if (!aviFile) {
        result.errorMessage = "Failed to create video file";
        SDLogger::getInstance().errorf("VideoRecorder: %s", result.errorMessage.c_str());
        restoreOriginalCameraSettings();
        _isRecording = false;
        return result;
    }

    // === Write AVI Header ===
    // RIFF header - size will be updated at the end
    size_t riffSizePos = 4;  // Position of RIFF size field
    writeFourCC(aviFile, "RIFF");
    writeU32(aviFile, 0);  // Placeholder for RIFF size
    writeFourCC(aviFile, "AVI ");

    // Calculate header sizes
    const uint32_t AVIH_SIZE = 56;  // AVI main header size
    const uint32_t STRH_SIZE = 56;  // Stream header size
    const uint32_t STRF_SIZE = 40;  // BITMAPINFOHEADER size
    const uint32_t STRL_LIST_SIZE = 4 + (8 + STRH_SIZE) + (8 + STRF_SIZE);  // strl + strh chunk + strf chunk
    const uint32_t HDRL_LIST_SIZE = 4 + (8 + AVIH_SIZE) + (8 + STRL_LIST_SIZE);  // hdrl + avih chunk + strl LIST

    // hdrl LIST
    writeFourCC(aviFile, "LIST");
    writeU32(aviFile, HDRL_LIST_SIZE);
    writeFourCC(aviFile, "hdrl");

    // avih chunk (main AVI header)
    size_t avihPos = aviFile.position() + 8;  // Position of avih data (after tag and size)
    writeFourCC(aviFile, "avih");
    writeU32(aviFile, AVIH_SIZE);

    // AVI Main Header data
    writeU32(aviFile, 1000000 / config.fps);  // dwMicroSecPerFrame
    writeU32(aviFile, width * height * 3);     // dwMaxBytesPerSec (estimate)
    writeU32(aviFile, 0);                      // dwPaddingGranularity
    writeU32(aviFile, 0x10);                   // dwFlags (AVIF_HASINDEX)
    size_t totalFramesPos = aviFile.position();
    writeU32(aviFile, 0);                      // dwTotalFrames - PLACEHOLDER
    writeU32(aviFile, 0);                      // dwInitialFrames
    writeU32(aviFile, 1);                      // dwStreams
    writeU32(aviFile, width * height * 3);     // dwSuggestedBufferSize
    writeU32(aviFile, width);                  // dwWidth
    writeU32(aviFile, height);                 // dwHeight
    writeU32(aviFile, 0);                      // dwReserved[0]
    writeU32(aviFile, 0);                      // dwReserved[1]
    writeU32(aviFile, 0);                      // dwReserved[2]
    writeU32(aviFile, 0);                      // dwReserved[3]

    // strl LIST (stream list)
    writeFourCC(aviFile, "LIST");
    writeU32(aviFile, STRL_LIST_SIZE);
    writeFourCC(aviFile, "strl");

    // strh chunk (stream header)
    size_t strhLengthPos = aviFile.position() + 8 + 20;  // Position of dwLength in strh
    writeFourCC(aviFile, "strh");
    writeU32(aviFile, STRH_SIZE);

    // Stream header data
    writeFourCC(aviFile, "vids");              // fccType
    writeFourCC(aviFile, "MJPG");              // fccHandler
    writeU32(aviFile, 0);                      // dwFlags
    writeU16(aviFile, 0);                      // wPriority
    writeU16(aviFile, 0);                      // wLanguage
    writeU32(aviFile, 0);                      // dwInitialFrames
    writeU32(aviFile, 1);                      // dwScale
    writeU32(aviFile, config.fps);             // dwRate
    writeU32(aviFile, 0);                      // dwStart
    size_t strhDwLengthPos = aviFile.position();
    writeU32(aviFile, 0);                      // dwLength - PLACEHOLDER
    writeU32(aviFile, width * height * 3);     // dwSuggestedBufferSize
    writeU32(aviFile, 10000);                  // dwQuality
    writeU32(aviFile, 0);                      // dwSampleSize
    writeU16(aviFile, 0);                      // rcFrame.left
    writeU16(aviFile, 0);                      // rcFrame.top
    writeU16(aviFile, width);                  // rcFrame.right
    writeU16(aviFile, height);                 // rcFrame.bottom

    // strf chunk (stream format - BITMAPINFOHEADER)
    writeFourCC(aviFile, "strf");
    writeU32(aviFile, STRF_SIZE);

    // BITMAPINFOHEADER
    writeU32(aviFile, STRF_SIZE);              // biSize
    writeU32(aviFile, width);                  // biWidth
    writeU32(aviFile, height);                 // biHeight
    writeU16(aviFile, 1);                      // biPlanes
    writeU16(aviFile, 24);                     // biBitCount
    writeFourCC(aviFile, "MJPG");              // biCompression
    writeU32(aviFile, width * height * 3);     // biSizeImage
    writeU32(aviFile, 0);                      // biXPelsPerMeter
    writeU32(aviFile, 0);                      // biYPelsPerMeter
    writeU32(aviFile, 0);                      // biClrUsed
    writeU32(aviFile, 0);                      // biClrImportant

    // movi LIST header
    size_t moviListPos = aviFile.position();
    size_t moviSizePos = moviListPos + 4;
    writeFourCC(aviFile, "LIST");
    writeU32(aviFile, 0);  // Placeholder for movi size
    writeFourCC(aviFile, "movi");

    size_t moviDataStart = aviFile.position();

    // === Record frames ===
    SDLogger::getInstance().infof("Starting video capture: %d fps, %d seconds", config.fps, config.durationSeconds);

    // Flush any stale frames
    for (int i = 0; i < 3; i++) {
        camera_fb_t* stale = esp_camera_fb_get();
        if (stale) esp_camera_fb_return(stale);
        delay(10);
    }

    unsigned long startTime = millis();
    unsigned long lastFrameTime = startTime;
    uint32_t frameCount = 0;

    while (frameCount < targetFrames && !_stopRequested) {
        unsigned long currentTime = millis();
        unsigned long elapsedSinceLastFrame = currentTime - lastFrameTime;

        // Wait for next frame interval
        if (elapsedSinceLastFrame < frameIntervalMs) {
            delay(frameIntervalMs - elapsedSinceLastFrame);
            currentTime = millis();
        }

        // Capture frame
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            SDLogger::getInstance().warnf("Frame capture failed at frame %d", frameCount);
            continue;
        }

        // Record frame position for index
        _frameIndex[frameCount].offset = aviFile.position() - moviDataStart;
        _frameIndex[frameCount].size = fb->len;

        // Write frame chunk: "00dc" + size + data
        writeFourCC(aviFile, "00dc");
        writeU32(aviFile, fb->len);
        aviFile.write(fb->buf, fb->len);

        // Pad to even boundary if needed
        if (fb->len & 1) {
            uint8_t pad = 0;
            aviFile.write(&pad, 1);
        }

        frameCount++;
        esp_camera_fb_return(fb);
        lastFrameTime = currentTime;

        // Progress callback
        if (callback) {
            callback(frameCount, targetFrames, currentTime - startTime);
        }

        yield();
    }

    unsigned long endTime = millis();
    result.durationMs = endTime - startTime;
    result.totalFrames = frameCount;

    SDLogger::getInstance().infof("Captured %d frames in %d ms", frameCount, result.durationMs);

    // Calculate movi size (including 'movi' tag)
    size_t moviEnd = aviFile.position();
    uint32_t moviSize = moviEnd - moviDataStart + 4;  // +4 for 'movi' tag

    // === Write index (idx1) ===
    writeFourCC(aviFile, "idx1");
    writeU32(aviFile, frameCount * 16);  // 16 bytes per entry

    for (uint32_t i = 0; i < frameCount; i++) {
        writeFourCC(aviFile, "00dc");          // ckid
        writeU32(aviFile, 0x10);               // dwFlags (AVIIF_KEYFRAME)
        writeU32(aviFile, _frameIndex[i].offset);  // dwChunkOffset (relative to movi)
        writeU32(aviFile, _frameIndex[i].size);    // dwChunkLength
    }

    // Get final file size
    size_t fileSize = aviFile.position();

    // === Update placeholders ===
    // Update RIFF size
    aviFile.seek(riffSizePos);
    writeU32(aviFile, fileSize - 8);

    // Update total frames in avih
    aviFile.seek(totalFramesPos);
    writeU32(aviFile, frameCount);

    // Update dwLength in strh
    aviFile.seek(strhDwLengthPos);
    writeU32(aviFile, frameCount);

    // Update movi LIST size
    aviFile.seek(moviSizePos);
    writeU32(aviFile, moviSize);

    aviFile.close();

    // Restore original camera settings
    restoreOriginalCameraSettings();

    _isRecording = false;
    result.success = true;
    result.fileSize = fileSize;

    SDLogger::getInstance().infof("Video recording complete: %s (%d bytes, %d frames)",
        result.filename.c_str(), result.fileSize, result.totalFrames);

    return result;
}

void VideoRecorder::stopRecording() {
    _stopRequested = true;
}

bool VideoRecorder::writeAviHeader(File& file, uint16_t width, uint16_t height, uint8_t fps, uint32_t totalFrames) {
    // Not used in new implementation
    return true;
}

bool VideoRecorder::writeFrameToAvi(File& file, camera_fb_t* frame, uint32_t frameIndex) {
    // Not used in new implementation
    return true;
}

bool VideoRecorder::finalizeAvi(File& file, uint32_t totalFrames, uint32_t moviSize) {
    // Not used in new implementation
    return true;
}

String VideoRecorder::generateFilename(const char* outputDir) {
    char filename[64];
    unsigned long timestamp = millis();
    snprintf(filename, sizeof(filename), "%s/video_%lu.avi", outputDir, timestamp);
    return String(filename);
}

void VideoRecorder::saveOriginalCameraSettings() {
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        _originalFrameSize = (framesize_t)sensor->status.framesize;
        _originalQuality = sensor->status.quality;
        SDLogger::getInstance().debugf("Saved camera settings: framesize=%d, quality=%d",
            _originalFrameSize, _originalQuality);
    }
}

void VideoRecorder::restoreOriginalCameraSettings() {
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_framesize(sensor, _originalFrameSize);
        sensor->set_quality(sensor, _originalQuality);
        SDLogger::getInstance().debugf("Restored camera settings: framesize=%d, quality=%d",
            _originalFrameSize, _originalQuality);
    }
}

bool VideoRecorder::setCameraForVideo(const VideoConfig& config) {
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) {
        SDLogger::getInstance().errorf("Failed to get camera sensor");
        return false;
    }

    if (sensor->set_framesize(sensor, config.frameSize) != 0) {
        SDLogger::getInstance().errorf("Failed to set frame size");
        return false;
    }

    if (sensor->set_quality(sensor, config.quality) != 0) {
        SDLogger::getInstance().errorf("Failed to set quality");
        return false;
    }

    SDLogger::getInstance().infof("Camera configured for video: framesize=%d, quality=%d",
        config.frameSize, config.quality);

    delay(100);
    return true;
}
