#include "LogUploader.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

#include "SDLogger.h"

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

LogUploader& LogUploader::getInstance() {
    static LogUploader instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void LogUploader::init(const char* host, const char* deviceName, const char* token) {
    _host        = host;
    _deviceName  = deviceName;
    _token       = token;
    _initialized = true;
}

void LogUploader::triggerUpload() {
    if (!_initialized) {
        SDLogger::getInstance().warnf("LogUploader: triggerUpload() called before init()");
        return;
    }
    if (_uploading) {
        SDLogger::getInstance().debugf("LogUploader: upload already in progress, skipping");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        SDLogger::getInstance().debugf("LogUploader: WiFi not connected, skipping upload");
        return;
    }

    _uploading = true;
    BaseType_t result = xTaskCreate(
        uploadTask,
        "LogUpload",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        nullptr
    );
    if (result != pdPASS) {
        SDLogger::getInstance().errorf("LogUploader: failed to create upload task");
        _uploading = false;
    }
}

// ---------------------------------------------------------------------------
// Background task
// ---------------------------------------------------------------------------

void LogUploader::uploadTask(void* parameter) {
    LogUploader* self = static_cast<LogUploader*>(parameter);

    SDLogger::getInstance().infof("LogUploader: background upload started");

    // Determine which file is currently being written — we must not touch it.
    String currentFile = SDLogger::getInstance().getCurrentLogFile();
    // currentFile is the full path, e.g. "/logs/0001_catcam_20260414_123456.log".
    // listLogFiles() returns bare filenames, so strip the prefix.
    int lastSlash = currentFile.lastIndexOf('/');
    String currentFilename = (lastSlash >= 0)
        ? currentFile.substring(lastSlash + 1)
        : currentFile;

    std::vector<String> files = SDLogger::getInstance().listLogFiles();
    int uploaded = 0;
    int failed   = 0;

    for (const String& filename : files) {
        if (filename == currentFilename) {
            SDLogger::getInstance().debugf("LogUploader: skipping active file %s", filename.c_str());
            continue;
        }

        String fullPath = String("/logs/") + filename;
        SDLogger::getInstance().infof("LogUploader: uploading %s", filename.c_str());

        if (self->uploadFile(fullPath)) {
            if (SD_MMC.remove(fullPath.c_str())) {
                SDLogger::getInstance().infof("LogUploader: uploaded and deleted %s", filename.c_str());
            } else {
                SDLogger::getInstance().warnf("LogUploader: uploaded but could not delete %s", filename.c_str());
            }
            uploaded++;
        } else {
            SDLogger::getInstance().warnf("LogUploader: upload failed for %s — will retry on next connect", filename.c_str());
            failed++;
        }
    }

    SDLogger::getInstance().infof("LogUploader: done — %d uploaded, %d failed", uploaded, failed);

    self->_uploading = false;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Single-file upload
// ---------------------------------------------------------------------------

bool LogUploader::uploadFile(const String& fullPath) {
    File f = SD_MMC.open(fullPath.c_str(), FILE_READ);
    if (!f) {
        SDLogger::getInstance().errorf("LogUploader: cannot open %s", fullPath.c_str());
        return false;
    }
    size_t fileSize = f.size();
    if (fileSize == 0) {
        f.close();
        SDLogger::getInstance().debugf("LogUploader: %s is empty, skipping", fullPath.c_str());
        // Treat empty files as successfully "uploaded" so they get cleaned up.
        return true;
    }

    WiFiClientSecure client;
    client.setInsecure();   // Internal server — certificate not verified
    client.setTimeout(CONNECT_TIMEOUT_S);

    SDLogger::getInstance().debugf("LogUploader: connecting to %s:443", _host);
    if (!client.connect(_host, 443)) {
        f.close();
        SDLogger::getInstance().errorf("LogUploader: connection to %s failed", _host);
        return false;
    }

    // Build request path: /logs/<deviceName>
    String path = String("/logs/") + _deviceName;

    // Send HTTP request headers
    client.printf("POST %s HTTP/1.1\r\n", path.c_str());
    client.printf("Host: %s\r\n", _host);
    client.printf("Authorization: Bearer %s\r\n", _token);
    client.println("Content-Type: text/plain");
    client.printf("Content-Length: %u\r\n", (unsigned int)fileSize);
    client.println("Connection: close");
    client.println();

    // Stream file body in chunks to avoid loading it all into RAM
    uint8_t buf[CHUNK_SIZE];
    size_t bytesSent = 0;
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        size_t written = client.write(buf, n);
        if (written != (size_t)n) {
            SDLogger::getInstance().errorf("LogUploader: short write (%d of %d) on %s", written, n, fullPath.c_str());
            f.close();
            client.stop();
            return false;
        }
        bytesSent += written;
        yield();
    }
    f.close();

    SDLogger::getInstance().debugf("LogUploader: sent %u bytes, waiting for response", (unsigned int)bytesSent);

    // Wait for server response
    unsigned long deadline = millis() + RESPONSE_TIMEOUT_MS;
    while (!client.available() && millis() < deadline) {
        delay(10);
    }
    if (!client.available()) {
        SDLogger::getInstance().errorf("LogUploader: response timeout from %s", _host);
        client.stop();
        return false;
    }

    // Parse status line: "HTTP/1.1 200 OK"
    String statusLine = client.readStringUntil('\n');
    int statusCode = 0;
    int spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx >= 0) {
        statusCode = statusLine.substring(spaceIdx + 1, spaceIdx + 4).toInt();
    }

    // Drain remaining response data
    while (client.available()) {
        client.read();
    }
    client.stop();

    SDLogger::getInstance().infof("LogUploader: server returned %d for %s", statusCode, fullPath.c_str());
    return statusCode == 200;
}
