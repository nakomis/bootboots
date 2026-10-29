Import("env")

import os
import re

# Firmware version (BOOT-10): set at build time from the FIRMWARE_VERSION environment variable,
# never hand-bumped in include/version.h. A local build without it is 0.0.0-dev.
#
#   FIRMWARE_VERSION=1.0.333 pio run -e esp32s3cam
version = os.environ.get("FIRMWARE_VERSION", "").strip() or "0.0.0-dev"
if not re.fullmatch(r"\d{1,4}\.\d{1,4}\.\d{1,4}(-[0-9A-Za-z.]+)?", version):
    raise SystemExit(f"FIRMWARE_VERSION must be x.y.z (optionally -suffix), not {version!r}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])
print(f"BootBoots firmware version {version}")

# Override the maximum upload size to match OTA0 partition
# PlatformIO defaults to the first app partition (factory)
# but we want to flash to OTA0 which is much larger
#
# Partition sizes:
# - ESP32-CAM (4MB):  OTA0 = 0x370000 = 3,604,480 bytes (3.44MB)
# - ESP32-S3 (16MB):  OTA0 = 0x700000 = 7,340,032 bytes (7MB)

def before_build(source, target, env):
    board = env.BoardConfig()
    board_name = board.get("name", "")

    # Check if this is an ESP32-S3 board
    if "s3" in board_name.lower() or "s3" in env.get("PIOENV", "").lower():
        # ESP32-S3 with 16MB flash: OTA0 = 7MB
        max_size = 7340032
    else:
        # Original ESP32-CAM with 4MB flash: OTA0 = 3.44MB
        max_size = 3604480

    board.update("upload.maximum_size", max_size)

env.AddPreAction("checkprogsize", before_build)
