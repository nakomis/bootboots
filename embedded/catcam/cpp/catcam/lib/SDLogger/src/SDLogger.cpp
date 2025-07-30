#include "SDLogger.h"
#include <time.h>
#include <sys/time.h>
#include <functional>

SDLogger& SDLogger::getInstance() {
    static SDLogger instance;
    return instance;
}

bool SDLogger::init(int csPin, const char* logDir) {
    if (_initialized) {
        return true;
    }
    
    _csPin = csPin;
    _logDir = String(logDir);
    
    // Create mutex for thread safety
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        Serial.println("SDLogger: Failed to create mutex");
        return false;
    }
    
    // Initialize SD card
    if (!SD.begin(_csPin)) {
        Serial.println("SDLogger: SD card initialization failed");
        return false;
    }
    
    // Create log directory
    if (!createLogDirectory()) {
        Serial.println("SDLogger: Failed to create log directory");
        return false;
    }
    
    // Generate initial log file name
    _currentLogFile = generateLogFileName();
    
    _initialized = true;
    
    // Log initialization
    info("SDLogger initialized successfully");
    infof("Log directory: %s", _logDir.c_str());
    infof("Current log file: %s", _currentLogFile.c_str());
    
    return true;
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

void SDLogger::debugf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    debug(buffer);
}

void SDLogger::infof(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    info(buffer);
}

void SDLogger::warnf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    warn(buffer);
}

void SDLogger::errorf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    error(buffer);
}

void SDLogger::criticalf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    critical(buffer);
}

void SDLogger::log(LogLevel level, const char* message) {
    if (level < _minLogLevel) {
        return;
    }
    
    String logEntry = formatLogEntry(level, message);
    
    // Always output to Serial for immediate visibility
    Serial.print(logEntry);
    
    // Write to SD card if initialized
    if (_initialized) {
        writeToFile(logEntry.c_str());
    }
}

void SDLogger::logf(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(level, buffer);
}

void SDLogger::logDeterrentActivation(const char* catName, float confidence, const float* allProbs) {
    criticalf("DETERRENT_ACTIVATED: %s (%.1f%%) - Probs:[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]",
              catName, confidence * 100,
              allProbs[0] * 100, allProbs[1] * 100, allProbs[2] * 100,
              allProbs[3] * 100, allProbs[4] * 100, allProbs[5] * 100);
}

void SDLogger::logDeterrentRejection(const char* catName, float confidence, const char* reason) {
    infof("DETERRENT_REJECTED: %s (%.1f%%) - %s", catName, confidence * 100, reason);
}

void SDLogger::logDetection(const char* catName, float confidence, int pictureNumber) {
    infof("DETECTION: %s (%.1f%%) - Picture #%d", catName, confidence * 100, pictureNumber);
}

void SDLogger::flush() {
    if (!_initialized) return;
    
    safeFileOperation([this]() {
        // SD.flush() is not available, but we can close and reopen to ensure data is written
        return true;
    });
}

void SDLogger::rotateLogs() {
    if (!_initialized) return;
    
    safeFileOperation([this]() {
        File currentFile = SD.open(_currentLogFile, FILE_READ);
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
    if (!_initialized) return;
    
    safeFileOperation([this, message]() {
        File logFile = SD.open(_currentLogFile, FILE_APPEND);
        if (logFile) {
            logFile.print(message);
            logFile.close();
            
            // Check if rotation is needed
            File sizeCheck = SD.open(_currentLogFile, FILE_READ);
            if (sizeCheck && sizeCheck.size() > _maxFileSize) {
                sizeCheck.close();
                rotateLogs();
            } else if (sizeCheck) {
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
    strftime(buffer, sizeof(buffer), "catcam_%Y%m%d_%H%M%S.log", timeinfo);
    
    return _logDir + "/" + String(buffer);
}

bool SDLogger::createLogDirectory() {
    if (!SD.exists(_logDir)) {
        return SD.mkdir(_logDir);
    }
    return true;
}

void SDLogger::cleanupOldLogs() {
    // Simple cleanup - remove oldest files if we exceed max count
    // This is a basic implementation; could be enhanced with proper sorting
    File root = SD.open(_logDir);
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
