#ifndef VERSION_H
#define VERSION_H

// Auto-generated version information
<<<<<<< Updated upstream
#define FIRMWARE_VERSION "1.0.312"
=======
#define FIRMWARE_VERSION "1.0.314"
>>>>>>> Stashed changes
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define PROJECT_NAME "BootBoots"

// Version components for programmatic access
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
<<<<<<< Updated upstream
#define VERSION_PATCH 312
=======
#define VERSION_PATCH 314
>>>>>>> Stashed changes

// Build a version string
#define VERSION_STRING PROJECT_NAME " v" FIRMWARE_VERSION " (" BUILD_TIMESTAMP ")"

#endif // VERSION_H