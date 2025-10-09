#ifndef VERSION_H
#define VERSION_H

// Bootloader version information
#define BOOTLOADER_VERSION "1.0.0"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define PROJECT_NAME "BootBoots-Bootloader"

// Version string
#define VERSION_STRING PROJECT_NAME " v" BOOTLOADER_VERSION " (" BUILD_TIMESTAMP ")"

#endif // VERSION_H
