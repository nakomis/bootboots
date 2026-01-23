#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
#include "Camera.h"

/**
 * ImageStorage - Manages image files on SD card
 *
 * Handles saving, organizing, and cleaning up captured images and their
 * associated metadata/response files.
 */
class ImageStorage {
public:
    /**
     * Initialize the image storage system
     * @param imagesDir Directory path for storing images (default: "/images")
     * @param maxImages Maximum number of image pairs to keep (default: 20)
     * @return true if initialization successful
     */
    bool init(const char* imagesDir = "/images", int maxImages = 20);

    /**
     * Generate a timestamp-based filename (without extension)
     * Format: 2026-01-18T11_28_33.179Z
     * @return Timestamp string suitable for use as a filename base
     */
    String generateFilename();

    /**
     * Save a captured image to SD card
     * @param basename Filename without extension
     * @param image Pointer to NamedImage containing JPEG data
     * @return true if save successful
     */
    bool saveImage(const String& basename, NamedImage* image);

    /**
     * Save a text response (e.g., AI inference result) to SD card
     * @param basename Filename without extension (will add .txt)
     * @param response Text content to save
     * @return true if save successful
     */
    bool saveResponse(const String& basename, const String& response);

    /**
     * Remove old image pairs, keeping only the most recent maxImages
     * Pairs consist of .jpg and .txt files with the same basename.
     * Will skip cleanup if system time appears invalid (year < 2000).
     */
    void cleanupOldImages();

    /**
     * Get the configured images directory path
     * @return Directory path string
     */
    const char* getImagesDir() const { return _imagesDir; }

    /**
     * Get the maximum images setting
     * @return Maximum number of image pairs to keep
     */
    int getMaxImages() const { return _maxImages; }

private:
    const char* _imagesDir = "/images";
    int _maxImages = 20;
    bool _initialized = false;
};
