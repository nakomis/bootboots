Import("env")

# Override the maximum upload size to match OTA0 partition (3.56MB)
# PlatformIO defaults to the first app partition (factory = 448KB)
# but we want to flash to OTA0 which is much larger
def before_build(source, target, env):
    env.BoardConfig().update("upload.maximum_size", 3604480)

env.AddPreAction("checkprogsize", before_build)
