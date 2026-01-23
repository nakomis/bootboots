#include "ImageStorage.h"
#include <SDLogger.h>
#include <time.h>
#include <vector>
#include <algorithm>

bool ImageStorage::init(const char* imagesDir, int maxImages) {
    _imagesDir = imagesDir;
    _maxImages = maxImages;

    // Create directory if it doesn't exist
    if (!SD_MMC.exists(_imagesDir)) {
        if (SD_MMC.mkdir(_imagesDir)) {
            SDLogger::getInstance().infof("Created images directory: %s", _imagesDir);
        } else {
            SDLogger::getInstance().errorf("Failed to create images directory: %s", _imagesDir);
            return false;
        }
    } else {
        SDLogger::getInstance().debugf("Images directory exists: %s", _imagesDir);
    }

    _initialized = true;
    return true;
}

String ImageStorage::generateFilename() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    char timestamp[32];
    int millis_part = tv.tv_usec / 1000;
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d_%02d_%02d.%03dZ",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec,
        millis_part);

    return String(timestamp);
}

bool ImageStorage::saveImage(const String& basename, NamedImage* image) {
    if (!image || !image->image || image->size == 0) {
        SDLogger::getInstance().errorf("Invalid image data");
        return false;
    }

    String filepath = String(_imagesDir) + "/" + basename + ".jpg";
    File file = SD_MMC.open(filepath.c_str(), FILE_WRITE);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open file for writing: %s", filepath.c_str());
        return false;
    }

    size_t written = file.write(image->image, image->size);
    file.close();

    if (written != image->size) {
        SDLogger::getInstance().errorf("Failed to write complete image: %d of %d bytes", written, image->size);
        return false;
    }

    SDLogger::getInstance().infof("Saved image: %s (%d bytes)", filepath.c_str(), image->size);
    return true;
}

bool ImageStorage::saveResponse(const String& basename, const String& response) {
    String filepath = String(_imagesDir) + "/" + basename + ".txt";
    File file = SD_MMC.open(filepath.c_str(), FILE_WRITE);
    if (!file) {
        SDLogger::getInstance().errorf("Failed to open file for writing: %s", filepath.c_str());
        return false;
    }

    size_t written = file.print(response);
    file.close();

    if (written == 0 && response.length() > 0) {
        SDLogger::getInstance().errorf("Failed to write response to file");
        return false;
    }

    SDLogger::getInstance().infof("Saved response: %s", filepath.c_str());
    return true;
}

void ImageStorage::cleanupOldImages() {
    // Safety check: don't delete if time sync failed (year in 1900s)
    time_t now;
    time(&now);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    if (timeinfo.tm_year + 1900 < 2000) {
        SDLogger::getInstance().warnf("Time sync appears failed (year=%d), skipping cleanup", timeinfo.tm_year + 1900);
        return;
    }

    // Collect all .jpg files in the images directory
    std::vector<String> imageFiles;

    File dir = SD_MMC.open(_imagesDir);
    if (!dir || !dir.isDirectory()) {
        SDLogger::getInstance().errorf("Failed to open images directory for cleanup");
        return;
    }

    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        if (name.endsWith(".jpg")) {
            imageFiles.push_back(name);
        }
        entry.close();
    }
    dir.close();

    // If we have more than _maxImages, delete the oldest ones
    if (imageFiles.size() <= (size_t)_maxImages) {
        SDLogger::getInstance().debugf("Image count (%d) within limit (%d), no cleanup needed",
            imageFiles.size(), _maxImages);
        return;
    }

    // Sort alphabetically (timestamp format ensures chronological order)
    std::sort(imageFiles.begin(), imageFiles.end());

    // Delete the oldest files (those at the beginning of the sorted list)
    int filesToDelete = imageFiles.size() - _maxImages;
    SDLogger::getInstance().infof("Cleaning up %d old image pairs", filesToDelete);

    for (int i = 0; i < filesToDelete; i++) {
        String jpgPath = String(_imagesDir) + "/" + imageFiles[i];
        String txtPath = jpgPath.substring(0, jpgPath.length() - 4) + ".txt";

        if (SD_MMC.remove(jpgPath.c_str())) {
            SDLogger::getInstance().debugf("Deleted: %s", jpgPath.c_str());
        } else {
            SDLogger::getInstance().warnf("Failed to delete: %s", jpgPath.c_str());
        }

        if (SD_MMC.exists(txtPath.c_str())) {
            if (SD_MMC.remove(txtPath.c_str())) {
                SDLogger::getInstance().debugf("Deleted: %s", txtPath.c_str());
            } else {
                SDLogger::getInstance().warnf("Failed to delete: %s", txtPath.c_str());
            }
        }
    }
}
