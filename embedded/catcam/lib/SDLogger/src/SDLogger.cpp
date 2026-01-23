#include "SDLogger.h"
#include <time.h>
#include <sys/time.h>
#include <functional>
#include <SD_MMC.h>
#include <FS.h>
#include <Preferences.h>

SDLogger& SDLogger::getInstance() {
    static SDLogger instance;
    return instance;
}

bool SDLogger::init(const char* logDir) {
    if (_initialized) {
        Serial.println("SDLogger: Already initialized");
        return true;
    }

#ifdef ESP32S3_CAM
    // ESP32-S3 CAM: Set custom SD card pins (SDMMC 1-bit mode)
    Serial.println("SDLogger: ESP32-S3 mode, setting SD pins 39,38,40");
    SD_MMC.setPins(39, 38, 40);  // CLK, CMD, D0
    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
        Serial.println("SDLogger: SD_MMC.begin() FAILED");
        return false;
    }
    Serial.println("SDLogger: SD_MMC.begin() OK");
#else
    // Original ESP32-CAM: Use default SDMMC 4-bit mode pins
    pinMode(14, PULLUP);
    pinMode(15, PULLUP);
    pinMode(2, PULLUP);
    pinMode(4, PULLUP);
    pinMode(12, PULLUP);
    pinMode(13, PULLUP);
    if (!SD_MMC.begin()) {
        // Serial.println("SDLogger: Failed to initialize SD_MMC");
        return false;
    }
#endif

    _logDir = String(logDir);

    // Read and increment boot counter from NVS
    Preferences prefs;
    prefs.begin("sdlogger", false);  // read-write
    _bootCounter = prefs.getUInt("boot_count", 0);
    _bootCounter++;  // Increment for this boot
    prefs.putUInt("boot_count", _bootCounter);
    prefs.end();

    // Create mutex for thread safety
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return false;
    }

    // Create log directory
    if (!createLogDirectory()) {
        return false;
    }

    // Generate initial log file name
    _currentLogFile = generateLogFileName();

    _initialized = true;

    // Log initialization
    info("SDLogger initialized successfully");
    infof("Boot count: %u", _bootCounter);
    infof("Log directory: %s", _logDir.c_str());
    infof("Current log file: %s", _currentLogFile.c_str());

    return true;
}

void SDLogger::trace(const char* message) {
    log(LOG_TRACE, message);
}

void SDLogger::debug(const char* message) {
    log(LOG_DEBUG, message);
}

void SDLogger::info(const char* message) {
    log(LOG_INFO, message);
}

void SDLogger::warn(const char* message) {
    log(LOG_WARN, message);
}

void SDLogger::error(const char* message) {
    log(LOG_ERROR, message);
}

void SDLogger::critical(const char* message) {
    log(LOG_CRITICAL, message);
}

void SDLogger::tracef(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    trace(buffer);
}

void SDLogger::debugf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    debug(buffer);
}

void SDLogger::infof(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    info(buffer);
}

void SDLogger::warnf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    warn(buffer);
}

void SDLogger::errorf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    error(buffer);
}

void SDLogger::criticalf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    critical(buffer);
}

void SDLogger::log(LogLevel level, const char* message) {
    if (level < _minLogLevel) {
        return;
    }

    String logEntry = formatLogEntry(level, message);

    // Write to SD card if initialized
    if (_initialized) {
        writeToFile(logEntry.c_str());
    }
    Serial.print(logEntry); // Also print to Serial for immediate feedback
}

void SDLogger::logf(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(level, buffer);
}

void SDLogger::flush() {
    if (!_initialized) return;

    safeFileOperation([this]() {
        // SD_MMC.flush() is not available, but we can close and reopen to ensure data is written
        return true;
        });
}

void SDLogger::rotateLogs() {
    if (!_initialized) return;

    safeFileOperation([this]() {
        File currentFile = SD_MMC.open(_currentLogFile, FILE_READ);
        if (currentFile && currentFile.size() > _maxFileSize) {
            currentFile.close();
            _currentLogFile = generateLogFileName();
            cleanupOldLogs();
            infof("Log rotated to: %s", _currentLogFile.c_str());
        }
        if (currentFile) currentFile.close();
        return true;
        });
}

void SDLogger::writeToFile(const char* message) {
    if (!_initialized || !_fileLoggingEnabled) return;

    safeFileOperation([this, message]() {
        fs::FS& fs = SD_MMC;
        File file = fs.open(_currentLogFile.c_str(), FILE_APPEND);
        if (file) {
            file.print(message);
            file.close();

            // Check if rotation is needed
            File sizeCheck = SD_MMC.open(_currentLogFile.c_str(), FILE_READ);
            if (sizeCheck && sizeCheck.size() > _maxFileSize) {
                sizeCheck.close();
                rotateLogs();
            }
            else if (sizeCheck) {
                sizeCheck.close();
            }
            return true;
        }
        return false;
        });
}

String SDLogger::formatLogEntry(LogLevel level, const char* message) {
    String timestamp = getCurrentTimestamp();
    String levelStr = getLogLevelString(level);
    return timestamp + " [" + levelStr + "] " + String(message) + "\n";
}

String SDLogger::getLogLevelString(LogLevel level) {
    switch (level) {
    case LOG_TRACE: return "TRACE";
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO: return "INFO";
    case LOG_WARN: return "WARN";
    case LOG_ERROR: return "ERROR";
    case LOG_CRITICAL: return "CRIT";
    default: return "UNKNOWN";
    }
}

String SDLogger::getCurrentTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    struct tm* timeinfo = localtime(&tv.tv_sec);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    return String(buffer) + "." + String(tv.tv_usec / 1000, DEC);
}

String SDLogger::generateLogFileName() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    struct tm* timeinfo = localtime(&tv.tv_sec);
    char buffer[64];
    // Format: BBBB_catcam_YYYYMMDD_HHMMSS.log (where BBBB is boot counter)
    snprintf(buffer, sizeof(buffer), "%04u_catcam_", _bootCounter);
    char timeBuffer[32];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y%m%d_%H%M%S.log", timeinfo);
    strcat(buffer, timeBuffer);

    return _logDir + "/" + String(buffer);
}

bool SDLogger::createLogDirectory() {
    if (!SD_MMC.exists(_logDir)) {
        return SD_MMC.mkdir(_logDir);
    }
    return true;
}

void SDLogger::cleanupOldLogs() {
    // Simple cleanup - remove oldest files if we exceed max count
    // This is a basic implementation; could be enhanced with proper sorting
    File root = SD_MMC.open(_logDir);
    if (!root || !root.isDirectory()) {
        return;
    }

    int fileCount = 0;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory() && String(file.name()).endsWith(".log")) {
            fileCount++;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    // If we have too many files, this is where we'd implement cleanup
    // For now, just log the count
    if (fileCount > _maxFiles) {
        warnf("Log directory has %d files (max: %d) - cleanup needed", fileCount, _maxFiles);
    }
}

String SDLogger::getRecentLogEntries(int maxLines) {
    if (!_initialized) {
        return "{\"error\":\"Logger not initialized\"}";
    }

    String result = "";
    int lineCount = 0;

    bool success = safeFileOperation([this, maxLines, &result, &lineCount]() {
        File file = SD_MMC.open(_currentLogFile.c_str(), FILE_READ);
        if (!file) {
            result = "{\"error\":\"Failed to open log file\"}";
            return false;
        }

        // If maxLines is -1, read all lines directly into result
        if (maxLines == -1) {
            result = "[";
            bool first = true;
            while (file.available()) {
                String line = file.readStringUntil('\n');
                if (line.length() > 0) {
                    if (!first) result += ",";
                    first = false;

                    // Escape quotes and backslashes in log line
                    line.replace("\\", "\\\\");
                    line.replace("\"", "\\\"");
                    result += "\"" + line + "\"";
                    lineCount++;
                }
            }
            result += "]";
            file.close();
            return true;
        }

        // Otherwise use circular buffer for last N lines
        String *lines = new String[maxLines];
        int currentLine = 0;
        int totalLines = 0;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.length() > 0) {
                lines[currentLine % maxLines] = line;
                currentLine++;
                totalLines++;
            }
        }
        file.close();

        // Build JSON array with the last N lines
        result = "[";
        int startIdx = (totalLines > maxLines) ? currentLine % maxLines : 0;
        int numLinesToReturn = (totalLines < maxLines) ? totalLines : maxLines;

        for (int i = 0; i < numLinesToReturn; i++) {
            int idx = (startIdx + i) % maxLines;
            if (i > 0) result += ",";

            // Escape quotes and backslashes in log line
            String escapedLine = lines[idx];
            escapedLine.replace("\\", "\\\\");
            escapedLine.replace("\"", "\\\"");

            result += "\"" + escapedLine + "\"";
        }
        result += "]";

        lineCount = numLinesToReturn;
        delete[] lines;
        return true;
    });

    if (!success) {
        return "{\"error\":\"Failed to read log entries\"}";
    }

    return result;
}

void SDLogger::processRecentLogEntries(int maxLines, std::function<void(const String&)> processor) {
    if (!_initialized) {
        processor("{\"error\":\"Logger not initialized\"}");
        return;
    }

    safeFileOperation([this, maxLines, &processor]() {
        File file = SD_MMC.open(_currentLogFile.c_str(), FILE_READ);
        if (!file) {
            processor("error - Failed to open log file");
            return false;
        }

        if (maxLines == -1) {
            // Process all lines from start to end
            while (file.available()) {
                String line = file.readStringUntil('\n');
                if (line.length() > 0) {
                    // Escape quotes and backslashes in log line
                    line.replace("\\", "\\\\");
                    line.replace("\"", "\\\"");
                    processor(line);
                }
            }
            file.close();
            return true;
        }

        // For limited lines, we need to read all lines to find the last N
        // But we'll use a circular buffer approach that doesn't store line contents
        // First pass: count total lines
        int totalLines = 0;
        while (file.available()) {
            file.readStringUntil('\n');
            totalLines++;
        }

        // Calculate which lines to process
        int linesToSkip = (totalLines > maxLines) ? (totalLines - maxLines) : 0;

        // Second pass: seek back to start and process the lines we want
        file.seek(0);
        int currentLine = 0;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (currentLine >= linesToSkip && line.length() > 0) {
                // Escape quotes and backslashes in log line
                line.replace("\\", "\\\\");
                line.replace("\"", "\\\"");
                processor(line);
            }
            currentLine++;
        }

        file.close();
        return true;
    });
}

bool SDLogger::safeFileOperation(std::function<bool()> operation) {
    if (!_initialized || _mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool result = operation();
        xSemaphoreGive(_mutex);
        return result;
    }

    return false;
}
