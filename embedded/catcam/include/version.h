#ifndef VERSION_H
#define VERSION_H

// The firmware version is set at build time (BOOT-10), never hand-bumped here: extra_script.py
// passes -DFIRMWARE_VERSION="x.y.z" from the FIRMWARE_VERSION environment variable, and a build
// without it says 0.0.0-dev. CI takes the version from the tracker.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-dev"
#endif

#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define PROJECT_NAME "BootBoots"

// What scripts/image-checks.sh looks for in a built image (with its terminating NUL), so a
// firmware.bin can be matched to the version it claims to be. main.cpp prints it at boot, which
// also keeps it in the image.
#define FIRMWARE_VERSION_TAG "bootboots-version:" FIRMWARE_VERSION

// Build a version string
#define VERSION_STRING PROJECT_NAME " v" FIRMWARE_VERSION " (" BUILD_TIMESTAMP ")"

#endif // VERSION_H
