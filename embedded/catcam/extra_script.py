Import("env")

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
