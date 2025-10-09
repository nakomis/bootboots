/**
 * BootBoots Minimal Bootloader (Factory App)
 *
 * This minimal "factory" app acts as a bootloader that:
 * 1. Checks NVS for pending OTA update flag
 * 2. If flag set, reads firmware from SD card and flashes to OTA0 partition
 * 3. Sets OTA0 as boot partition and reboots
 * 4. If no pending update, boots directly into OTA0 (main app)
 *
 * This approach gives us a single large OTA partition (~3.8MB) instead of
 * two smaller partitions (~1.9MB each), maximizing firmware growth potential.
 */

#include <Arduino.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "version.h"

// Constants
#define FIRMWARE_FILE "/firmware_update.bin"
#define FLASH_BUFFER_SIZE 512
#define LED_PIN 33  // Built-in LED on ESP32-CAM

// Global objects
Preferences prefs;

void blinkLED(int times, int delayMs = 200) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

void setup() {
    // Initialize serial for diagnostics
    Serial.begin(115200);
    delay(500);

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("\n\n========================================");
    Serial.println(VERSION_STRING);
    Serial.println("========================================\n");

    // Blink LED to indicate bootloader is running
    blinkLED(3, 100);

    // Check for pending OTA update
    prefs.begin("ota", true);  // Read-only
    bool pendingOTA = prefs.getBool("pending", false);
    size_t firmwareSize = prefs.getUInt("size", 0);
    prefs.end();

    if (!pendingOTA) {
        Serial.println("[BOOTLOADER] No pending OTA update");

        // Check if OTA0 partition exists and has valid app
        const esp_partition_t* ota0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_OTA_0,
            NULL
        );

        if (ota0 != NULL) {
            // Check if otadata already points to OTA0
            const esp_partition_t* boot_partition = esp_ota_get_boot_partition();

            if (boot_partition != ota0) {
                // First boot or otadata was erased - set OTA0 as boot partition
                Serial.println("[BOOTLOADER] Setting OTA0 as boot partition for first time");
                esp_ota_set_boot_partition(ota0);
            }

            Serial.println("[BOOTLOADER] Booting into main application (OTA0)...\n");
            delay(500);
            ESP.restart();
        } else {
            Serial.println("[BOOTLOADER] ERROR: OTA0 partition not found!");
            Serial.println("[BOOTLOADER] System halted.");
            while(1) {
                blinkLED(1, 1000);  // Slow blink to indicate error
            }
        }
    }

    Serial.printf("[BOOTLOADER] Pending OTA update detected (size: %u bytes)\n", firmwareSize);
    Serial.println("[BOOTLOADER] Starting OTA flash from SD card...\n");

    // Blink LED rapidly during OTA
    blinkLED(10, 50);

    // Initialize SD card
    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
        Serial.println("[BOOTLOADER] ERROR: SD card initialization failed!");

        // Clear the pending flag to prevent boot loop
        prefs.begin("ota", false);
        prefs.putBool("pending", false);
        prefs.end();

        Serial.println("[BOOTLOADER] Cleared pending flag to prevent boot loop");
        Serial.println("[BOOTLOADER] Rebooting into main app...\n");
        delay(2000);
        ESP.restart();
    }

    Serial.println("[BOOTLOADER] SD card initialized");

    // Check if firmware file exists
    if (!SD_MMC.exists(FIRMWARE_FILE)) {
        Serial.printf("[BOOTLOADER] ERROR: Firmware file not found: %s\n", FIRMWARE_FILE);

        // Clear the pending flag
        prefs.begin("ota", false);
        prefs.putBool("pending", false);
        prefs.end();

        Serial.println("[BOOTLOADER] Cleared pending flag");
        Serial.println("[BOOTLOADER] Rebooting into main app...\n");
        SD_MMC.end();
        delay(2000);
        ESP.restart();
    }

    File firmware = SD_MMC.open(FIRMWARE_FILE, FILE_READ);
    if (!firmware) {
        Serial.printf("[BOOTLOADER] ERROR: Failed to open firmware file: %s\n", FIRMWARE_FILE);

        prefs.begin("ota", false);
        prefs.putBool("pending", false);
        prefs.end();

        Serial.println("[BOOTLOADER] Cleared pending flag");
        Serial.println("[BOOTLOADER] Rebooting into main app...\n");
        SD_MMC.end();
        delay(2000);
        ESP.restart();
    }

    size_t fileSize = firmware.size();
    Serial.printf("[BOOTLOADER] Firmware file size: %u bytes\n", fileSize);

    if (firmwareSize > 0 && fileSize != firmwareSize) {
        Serial.printf("[BOOTLOADER] WARNING: File size mismatch (expected: %u, actual: %u)\n",
                     firmwareSize, fileSize);
    }

    // Get OTA0 partition
    const esp_partition_t* ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        NULL
    );

    if (ota0 == NULL) {
        Serial.println("[BOOTLOADER] ERROR: OTA0 partition not found!");
        firmware.close();
        SD_MMC.end();

        prefs.begin("ota", false);
        prefs.putBool("pending", false);
        prefs.end();

        Serial.println("[BOOTLOADER] System halted.");
        while(1) {
            blinkLED(1, 1000);
        }
    }

    Serial.printf("[BOOTLOADER] OTA0 partition: label=%s, size=%u bytes\n",
                 ota0->label, ota0->size);

    if (fileSize > ota0->size) {
        Serial.printf("[BOOTLOADER] ERROR: Firmware too large (%u bytes) for partition (%u bytes)\n",
                     fileSize, ota0->size);
        firmware.close();
        SD_MMC.end();

        prefs.begin("ota", false);
        prefs.putBool("pending", false);
        prefs.end();

        Serial.println("[BOOTLOADER] System halted.");
        while(1) {
            blinkLED(1, 1000);
        }
    }

    // Clear the pending flag BEFORE flashing (prevents boot loops on failure)
    prefs.begin("ota", false);
    prefs.putBool("pending", false);
    prefs.putUInt("size", 0);
    prefs.end();
    Serial.println("[BOOTLOADER] Cleared pending OTA flag");

    // Begin OTA
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(ota0, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[BOOTLOADER] ERROR: esp_ota_begin failed: %s\n", esp_err_to_name(err));
        firmware.close();
        SD_MMC.end();

        Serial.println("[BOOTLOADER] Rebooting into main app...\n");
        delay(2000);
        ESP.restart();
    }

    Serial.println("[BOOTLOADER] OTA begin successful, starting flash...");

    // Flash firmware in chunks
    uint8_t buffer[FLASH_BUFFER_SIZE];
    size_t bytesWritten = 0;
    size_t lastProgress = 0;

    while (firmware.available()) {
        size_t bytesToRead = min((size_t)FLASH_BUFFER_SIZE, (size_t)firmware.available());
        size_t bytesRead = firmware.read(buffer, bytesToRead);

        if (bytesRead > 0) {
            err = esp_ota_write(ota_handle, buffer, bytesRead);
            if (err != ESP_OK) {
                Serial.printf("[BOOTLOADER] ERROR: esp_ota_write failed: %s\n", esp_err_to_name(err));
                esp_ota_abort(ota_handle);
                firmware.close();
                SD_MMC.end();

                Serial.println("[BOOTLOADER] Rebooting into main app...\n");
                delay(2000);
                ESP.restart();
            }

            bytesWritten += bytesRead;

            // Print progress every 10%
            size_t progress = (bytesWritten * 100) / fileSize;
            if (progress >= lastProgress + 10) {
                Serial.printf("[BOOTLOADER] Flash progress: %u%% (%u/%u bytes)\n",
                             progress, bytesWritten, fileSize);
                blinkLED(1, 50);  // Quick blink for progress
                lastProgress = progress;
            }
        }
    }

    Serial.printf("[BOOTLOADER] Flash complete: %u bytes written\n", bytesWritten);

    firmware.close();

    // End OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[BOOTLOADER] ERROR: esp_ota_end failed: %s\n", esp_err_to_name(err));
        SD_MMC.end();

        Serial.println("[BOOTLOADER] Rebooting into main app...\n");
        delay(2000);
        ESP.restart();
    }

    Serial.println("[BOOTLOADER] OTA end successful");

    // Set OTA0 as boot partition
    err = esp_ota_set_boot_partition(ota0);
    if (err != ESP_OK) {
        Serial.printf("[BOOTLOADER] ERROR: esp_ota_set_boot_partition failed: %s\n", esp_err_to_name(err));
        SD_MMC.end();

        Serial.println("[BOOTLOADER] Rebooting anyway...\n");
        delay(2000);
        ESP.restart();
    }

    Serial.println("[BOOTLOADER] Boot partition set to OTA0");

    // Delete firmware file to save space
    if (SD_MMC.remove(FIRMWARE_FILE)) {
        Serial.printf("[BOOTLOADER] Deleted firmware file: %s\n", FIRMWARE_FILE);
    } else {
        Serial.printf("[BOOTLOADER] WARNING: Failed to delete firmware file: %s\n", FIRMWARE_FILE);
    }

    SD_MMC.end();

    Serial.println("\n========================================");
    Serial.println("[BOOTLOADER] OTA UPDATE SUCCESSFUL!");
    Serial.println("[BOOTLOADER] Rebooting into new firmware...");
    Serial.println("========================================\n");

    // Success blink
    blinkLED(5, 100);

    delay(2000);
    ESP.restart();
}

void loop() {
    // Should never reach here - bootloader always reboots
    delay(10000);
}
