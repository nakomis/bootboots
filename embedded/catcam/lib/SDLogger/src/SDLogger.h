#ifndef CATCAM_SDLOGGER_H
#define CATCAM_SDLOGGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum LogLevel {
    LOG_TRACE = -1,
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_CRITICAL = 4
};

class SDLogger {
public:
    static SDLogger& getInstance();
    
    // Initialize SD card and logger
    bool init(const char* logDir = "/logs");
    
    // Thread-safe logging methods
    void trace(const char* message);
    void debug(const char* message);
    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);
    void critical(const char* message);
    
    // Formatted logging
    void tracef(const char* format, ...);
    void debugf(const char* format, ...);
    void infof(const char* format, ...);
    void warnf(const char* format, ...);
    void errorf(const char* format, ...);
    void criticalf(const char* format, ...);
    
    // Generic log method
    void log(LogLevel level, const char* message);
    void logf(LogLevel level, const char* format, ...);
    
    // Deterrent-specific logging
    void logDeterrentActivation(const char* catName, float confidence, const float* allProbs);
    void logDeterrentRejection(const char* catName, float confidence, const char* reason);
    void logDetection(const char* catName, float confidence, int pictureNumber);
    
    // File management
    void flush();
    void rotateLogs();
    bool isInitialized() const { return _initialized; }

    // Log retrieval
    String getRecentLogEntries(int maxLines = 50);
    void processRecentLogEntries(int maxLines, std::function<void(const String&)> processor);

    // Configuration
    void setLogLevel(LogLevel minLevel) { _minLogLevel = minLevel; }
    void setMaxFileSize(size_t maxSize) { _maxFileSize = maxSize; }
    void setMaxFiles(int maxFiles) { _maxFiles = maxFiles; }
    void setFileLoggingEnabled(bool enabled) { _fileLoggingEnabled = enabled; }

private:
    SDLogger() = default;
    ~SDLogger() = default;
    SDLogger(const SDLogger&) = delete;
    SDLogger& operator=(const SDLogger&) = delete;
    
    bool _initialized = false;
    String _logDir = "/logs";
    String _currentLogFile;
    LogLevel _minLogLevel = LOG_INFO;
    size_t _maxFileSize = 1024 * 1024; // 1MB default
    int _maxFiles = 10;
    bool _fileLoggingEnabled = true;
    uint32_t _bootCounter = 0;

    // Thread safety
    SemaphoreHandle_t _mutex = nullptr;
    
    // Internal methods
    void writeToFile(const char* message);
    String formatLogEntry(LogLevel level, const char* message);
    String getLogLevelString(LogLevel level);
    String getCurrentTimestamp();
    String generateLogFileName();
    bool createLogDirectory();
    void cleanupOldLogs();
    
    // Thread-safe wrapper for file operations
    bool safeFileOperation(std::function<bool()> operation);
};

// Convenience macros for easier logging
#define LOG_T(msg) SDLogger::getInstance().trace(msg)
#define LOG_D(msg) SDLogger::getInstance().debug(msg)
#define LOG_I(msg) SDLogger::getInstance().info(msg)
#define LOG_W(msg) SDLogger::getInstance().warn(msg)
#define LOG_E(msg) SDLogger::getInstance().error(msg)
#define LOG_C(msg) SDLogger::getInstance().critical(msg)

#define LOG_TF(fmt, ...) SDLogger::getInstance().tracef(fmt, ##__VA_ARGS__)
#define LOG_DF(fmt, ...) SDLogger::getInstance().debugf(fmt, ##__VA_ARGS__)
#define LOG_IF(fmt, ...) SDLogger::getInstance().infof(fmt, ##__VA_ARGS__)
#define LOG_WF(fmt, ...) SDLogger::getInstance().warnf(fmt, ##__VA_ARGS__)
#define LOG_EF(fmt, ...) SDLogger::getInstance().errorf(fmt, ##__VA_ARGS__)
#define LOG_CF(fmt, ...) SDLogger::getInstance().criticalf(fmt, ##__VA_ARGS__)

#endif
