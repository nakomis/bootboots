# BootBoots Firmware Build & Deployment Guide

Since BOOT-10 the firmware image holds no secrets and no per-device or per-environment settings:
each device reads them from its `prov` partition. The details (partition layout, keys, scripts,
the first wired flash) are in [docs/provisioning.md](docs/provisioning.md).

## Quick Start

```bash
cd embedded/catcam

# Build (the version comes from FIRMWARE_VERSION; default 0.0.0-dev)
FIRMWARE_VERSION=1.0.333 pio run -e esp32s3cam

# Check it as a publish would (version tag, no PEM blocks, no per-device strings, no Wi-Fi credentials)
./scripts/image-checks.sh .pio/build/esp32s3cam/firmware.bin 1.0.333

# Publish to sandbox (runs the checks again; never replaces a published version)
./scripts/publish-firmware.sh --dry-run sandbox .pio/build/esp32s3cam/firmware.bin 1.0.333
./scripts/publish-firmware.sh sandbox .pio/build/esp32s3cam/firmware.bin 1.0.333
```

Then push it to the device through Fleet.

## Prerequisites

- **PlatformIO** (`pip install platformio`); its esptool and Python are used by the scripts
- **AWS CLI** with the `nakom.is-sandbox` profile (`nakom.is-prod` for prod)
- For `provision.sh`: a Python with ESP-IDF's NVS partition generator (`esp_idf_nvs_partition_gen`;
  the IDF env under `~/.espressif/python_env/` has it, or set `NVS_GEN_PYTHON`)

## Versions

- Set at build time from `FIRMWARE_VERSION` by `extra_script.py`; `include/version.h` only holds the
  `0.0.0-dev` fallback and is never bumped by hand.
- The image carries `bootboots-version:<version>`, printed at boot, which `image-checks.sh` matches.
- Published versions are plain `x.y.z`. The last hand-published version was 1.0.331; 1.0.332 was
  committed but never published, so published versions continue from 1.0.333.

## S3 layout

```
nakomis-firmware-{sandbox,prod}/
└── BootBoots/
    ├── manifest.json          maintained by the bucket's Lambda; Fleet reads it
    └── <x.y.z>/firmware.bin   metadata: project, version, sha256, size
```

`publish-firmware.sh` uploads with `--if-none-match '*'`: a version, once published, is never
replaced. It never writes the manifest.

## Provisioning a device

```bash
./scripts/provision.sh BootBootsThing /dev/cu.usbmodemXXXX sandbox     # build + flash prov
./scripts/provision.sh BootBootsThing sandbox --image-only /private/dir/prov.bin   # image only
```

A device without a prov partition logs `NOT PROVISIONED` and runs with BLE only.

## USB flashing

- App only (0x110000): `scripts/build_and_flash.sh [port]`
- Factory-new device (bootloader, partition table, factory app, app): `scripts/factory_setup.sh <port>`,
  then `provision.sh`
- Close serial monitors first; flash S3s with `esptool --after watchdog_reset`.

## Troubleshooting

- **`NOT PROVISIONED` at boot:** the prov partition is missing (old partition table: flash the new
  `partitions.bin` at 0x8000), not an NVS image, or lacks a key; the log line says which. Run
  `provision.sh`.
- **No Wi-Fi:** check `/Nakomis/Wifi/*` in SSM and re-provision; the network must be 2.4 GHz.
- **`image-checks: FAIL`:** the message says which rule; a version mismatch usually means the build
  wasn't given `FIRMWARE_VERSION`.
