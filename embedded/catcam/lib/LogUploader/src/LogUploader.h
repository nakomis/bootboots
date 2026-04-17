#pragma once
#include <Arduino.h>

/**
 * LogUploader — uploads completed SD log files to logs.nasbox.nakomis.com.
 *
 * After init(), call triggerUpload() whenever WiFi becomes available.
 * The upload runs in a background FreeRTOS task so it never blocks the main loop.
 *
 * Only files that are NOT the currently-active log file are uploaded; the active
 * file is excluded because SDLogger is still writing to it.  Once a previous
 * session's log file is successfully uploaded (HTTP 200) it is deleted from the
 * SD card to reclaim space.
 */
class LogUploader {
public:
    static LogUploader& getInstance();

    /**
     * Provide connection details.  Call once after WiFi credentials are known,
     * before the first triggerUpload().
     *
     * @param host       Hostname of the log server, e.g. "logs.nasbox.nakomis.com"
     * @param deviceName Path component used in the POST URL, e.g. "bootboots"
     * @param token      Bearer token that matches LOG_TOKEN on the server
     */
    void init(const char* host, const char* deviceName, const char* token);

    /**
     * Kick off a background upload of all pending log files.
     * A no-op if an upload is already in progress or init() has not been called.
     */
    void triggerUpload();

    bool isUploading() const { return _uploading; }

private:
    LogUploader() = default;
    ~LogUploader() = default;
    LogUploader(const LogUploader&) = delete;
    LogUploader& operator=(const LogUploader&) = delete;

    const char* _host       = nullptr;
    const char* _deviceName = nullptr;
    const char* _token      = nullptr;
    volatile bool _uploading    = false;
    volatile bool _initialized  = false;

    static void uploadTask(void* parameter);
    bool uploadFile(const String& fullPath);

    static const int TASK_STACK_SIZE = 8192;
    static const int TASK_PRIORITY   = 1;
    static const int CONNECT_TIMEOUT_S = 30;
    static const int RESPONSE_TIMEOUT_MS = 15000;
    static const int CHUNK_SIZE = 1024;
};
