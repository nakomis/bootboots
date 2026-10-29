# Catcam provisioning (BOOT-10)

Until BOOT-10 the catcam firmware compiled `include/secrets.h` in: the IoT private key and
certificate, the Wi-Fi SSID and password, the IoT and credentials-provider endpoints, and (in
`main.cpp`) the sandbox API host and the Thing name. Every published `firmware.bin` carried them
(1.0.327, 1.0.328 and 1.0.331 in `s3://nakomis-firmware-sandbox/BootBoots/` do), and one image
could only ever serve one device in one environment.

Now the image holds none of that. Each device's identity, credentials and environment live in a
small **`prov`** partition that `scripts/provision.sh` writes over USB; the firmware reads it at
boot (`lib/Provisioning`). Every catcam runs the same image, and a published image is safe to
publish.

## Where the data lives: the choice

Two options were on the table.

| | (a) new `prov` partition (chosen) | (b) a `prov` namespace inside the existing `nvs` |
|---|---|---|
| Layout change | Yes: a 64 KB NVS partition carved from the front of `spiffs` | None |
| Provisioning a device | Write one generated NVS image at the prov offset | No host tool can add a namespace to a live NVS partition without rewriting the whole partition; it needs on-device code (a BLE/serial "provision" command) or a read-modify-write of the whole `nvs` |
| Risk to existing NVS (training mode, threshold, dry-run, Claude inference, camera settings, OTA flag, Wi-Fi/BLE state) | None: `nvs` is never written | Real: the only host-side route rewrites it |
| Space | 64 KB to itself (certificate + key + root CA ≈ 4 KB) | `nvs` is 20 KB (5 pages, one kept free) and already holds settings, PHY calibration and Wi-Fi state |
| Re-provisioning / wiping | Rewrite or erase one partition | Delete keys from on-device code |
| Matches | tinycrypt's prov partition (CRYPT-14) | |

**Choice: (a).** `spiffs` is genuinely unused: nothing in `embedded/` mounts SPIFFS or LittleFS
or looks the partition up (grepped for `SPIFFS`, `LittleFS`, `esp_partition`, `"spiffs"`), so
taking its first 64 KB costs nothing. Every other partition keeps its offset and size, so the
factory app, the SD-card OTA flow and the NVS contents are untouched. The only cost is that the
partition table has to be written once over USB, which the first wired flash does anyway.

## Partition table (ESP32-S3, 16 MB)

`partitions_custom_s3.csv` and `../bootloader/partitions_bootloader_s3.csv` are identical (the
two builds produce byte-identical `partitions.bin`).

| Name | Type / subtype | Offset | Size | Change |
|---|---|---|---|---|
| (2nd-stage bootloader) | | 0x0 | | unchanged |
| (partition table) | | 0x8000 | 0xC00 | **rewritten** |
| nvs | data / nvs | 0x9000 | 0x5000 (20 KB) | unchanged, never written by any step |
| otadata | data / ota | 0xE000 | 0x2000 | unchanged |
| factory | app / factory | 0x10000 | 0x100000 (1 MB) | unchanged |
| ota_0 | app / ota_0 | 0x110000 | 0x700000 (7 MB) | unchanged |
| **prov** | **data / nvs** | **0x810000** | **0x10000 (64 KB)** | **new** (was the start of spiffs) |
| spiffs | data / spiffs | 0x820000 (was 0x810000) | 0x7D0000 (was 0x7E0000) | shrunk by 64 KB; unused |
| coredump | data / coredump | 0xFF0000 | 0x10000 | unchanged |

Offsets checked: the factory app finds `ota_0` with `esp_partition_find_first(APP, OTA_0)` and
its OTA flag through `Preferences` on the default `nvs` partition, so it needs no change and no
re-flash. `build_and_flash.sh` (0x110000) and `factory_setup.sh` (0x0, 0x8000, 0x10000,
0x110000) write nothing that moved; the only stale offset was factory_setup's SPIFFS banner line.
The legacy 4 MB tables (`partitions_custom.csv`, `partitions_bootloader.csv`, env `esp32cam`,
commented out in catcam's `platformio.ini`) have no prov partition; a legacy build would run
unprovisioned.

## What's in it

NVS namespace `prov`, all strings (`lib/Provisioning/src/Provisioning.cpp`):

| Key | Source (provision.sh) | Required |
|---|---|---|
| `thing` | the `<thing>` argument; also the MQTT client id and `x-amzn-iot-thingname` | yes |
| `wifi_ssid`, `wifi_pass` | SSM `/Nakomis/Wifi/SSID`, `/Nakomis/Wifi/Password` (String; sandbox account, `WIFI_PROFILE`) | yes |
| `iot_endpoint` | `aws iot describe-endpoint --endpoint-type iot:Data-ATS` | yes |
| `cred_endpoint` | `aws iot describe-endpoint --endpoint-type iot:CredentialProvider` | yes |
| `api_host` | `API_HOST`; default `api.bootboots.sandbox.nakomis.com` for sandbox, required for prod | yes |
| `root_ca` | `AmazonRootCA1.pem` | yes |
| `cert`, `key` | SSM `/BootsBoots/<thing>/certPem`, `/BootsBoots/<thing>/privKey` (both String, not SecureString) | yes |
| `role_alias` | `ROLE_ALIAS`, default `BootBootsRoleAlias` | no (firmware defaults the same) |
| `region` | `AWS_REGION`, default `eu-west-2` | no (firmware defaults the same) |
| `env` | the `[env]` argument; logged only | no |

The root CA is in prov too, although it's public: it keeps every PEM block out of the image, so
image-checks can simply refuse any.

**Unprovisioned:** if the partition is missing, isn't NVS, or lacks a required key, the device
prints `[PROV] NOT PROVISIONED: <why>. No networking; BLE only.` on serial (and logs it to SD),
skips Wi-Fi, AWS credentials, the camera pipeline and MQTT, and still starts BLE (status, OTA
service, commands), so it can be diagnosed.

**Not secret-proof:** prov is plain flash, like the old image was. Anyone with the device and a
USB cable can read the key and the Wi-Fi password; what's gained is that a published image no
longer gives them away.

## Scripts

All in `embedded/catcam/scripts/`.

- `provision.sh <thing> <port> [env]` (default env `sandbox`, `AWS_PROFILE` default
  `nakom.is-<env>`): fetches the data above into a private `mktemp -d` (removed by a trap),
  builds the NVS image with ESP-IDF's generator (`esp_idf_nvs_partition_gen`; the PlatformIO
  `framework-espidf` copy of `nvs_partition_gen.py` is a shim for it, so it needs a Python that has
  the module: the IDF env under `~/.espressif/python_env/`, or `NVS_GEN_PYTHON`), reads it back
  with ESP-IDF's NVS parser (integrity check, every key present; names only), checks the device's
  partition table has `prov` at 0x810000, then writes it with `esptool --after watchdog_reset`.
  Secrets never reach argv, stdout or any other file.
- `provision.sh <thing> [env] --image-only OUT`: builds and checks the image, writes it to OUT
  (mode 0600) and stops. OUT holds the private key and Wi-Fi password: keep it private and delete it.
- `image-checks.sh <firmware.bin> <version>`: the image has the `bootboots-version:<version>` tag
  and no other; no PEM block (a `-----BEGIN/END ...-----` marker on its own line; mbedTLS's own
  NUL-terminated `-----BEGIN CERTIFICATE-----` and `-----END ... KEY-----` constants are in every
  image, so a bare `-----BEGIN` can't be the test); none of `BootBootsThing`, `-ats.iot.`,
  `.credentials.iot.`, `api.bootboots.`; neither the Wi-Fi SSID nor password (fetched from SSM at
  check time, passed to the scanner through its environment, never echoed). The old 1.0.331 image
  fails all of these.
- `publish-firmware.sh [--dry-run] <sandbox|prod> <firmware.bin> <x.y.z>`: runs image-checks,
  then `aws s3api put-object --if-none-match '*'` to
  `s3://nakomis-firmware-<env>/BootBoots/<x.y.z>/firmware.bin` with metadata `project`, `version`,
  `sha256`, `size`. The bucket's Lambda maintains `BootBoots/manifest.json` (what Fleet reads);
  the script never writes it. Same bytes again: a no-op; other bytes under a published version:
  refused.
- `build_and_upload.py` (hand-bumped version.h, built with secrets, uploaded) is gone;
  `generate_secrets.py` and `include/secrets.h.template` too: nothing includes `secrets.h` any more.

## Firmware version

Set at build time: `extra_script.py` turns the `FIRMWARE_VERSION` environment variable into
`-DFIRMWARE_VERSION="..."` (default `0.0.0-dev`; `x.y.z`, optionally `-suffix`). `include/version.h`
only supplies the fallback and is never bumped by hand. The image carries
`bootboots-version:<version>` (printed at boot), which is what image-checks matches: the ESP-IDF
app descriptor can't be used, as the precompiled Arduino core fills it with the lib-builder's own
version (`esp-idf: v4.4.7 ...`).

```bash
FIRMWARE_VERSION=1.0.333 pio run -d embedded/catcam -e esp32s3cam
```

**Version tracker:** the last hand-published version is **1.0.331**; `version.h` was bumped to
**1.0.332** and committed, but 1.0.332 was never published. Seed the CI version tracker so its
first version is **1.0.333 or later** (i.e. above 1.0.332), so no number is ever reused.

## First wired flash after BOOT-10 (per device)

Only needed once per device, for the partition table and prov; afterwards apps go through Fleet.
Paths are from the repo root; `ESPTOOL="$HOME/.platformio/penv/bin/python $HOME/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port <port>"`.

1. **Build and check.** `pio run -d embedded/bootloader -e esp32s3cam` and
   `FIRMWARE_VERSION=<v> pio run -d embedded/catcam -e esp32s3cam`, then
   `embedded/catcam/scripts/image-checks.sh embedded/catcam/.pio/build/esp32s3cam/firmware.bin <v>`.
2. **Close every serial monitor** (`pio device monitor`, screen, the Arduino IDE): they hold the port.
3. **Back up**, into a private directory (`mkdir -m 700 ~/bootboots-backup-$(date +%F)`):
   - `$ESPTOOL read_flash 0x9000 0x5000 nvs.bin` (the settings; it also holds Wi-Fi state, so keep it private)
   - `$ESPTOOL read_flash 0x8000 0xc00 partitions-old.bin`
   - `$ESPTOOL read_flash 0xe000 0x2000 otadata.bin`
   - `$ESPTOOL read_flash 0x110000 0x700000 ota_0-old.bin` (the running app, for rollback; it holds the old compiled-in secrets)
   - Optional look (namespaces only, no values): `~/.platformio/penv/bin/python ~/.platformio/packages/framework-espidf/components/nvs_flash/nvs_partition_tool/nvs_tool.py -d namespaces nvs.bin`
4. **Write the partition table and the app** (no reset yet):
   `$ESPTOOL --after no_reset write_flash 0x8000 embedded/catcam/.pio/build/esp32s3cam/partitions.bin 0x110000 embedded/catcam/.pio/build/esp32s3cam/firmware.bin`.
   Neither touches 0x9000-0xDFFF (nvs), otadata, the factory app or the bootloader.
5. **Provision:** `embedded/catcam/scripts/provision.sh BootBootsThing <port> sandbox`
   (checks the new table is there, writes prov at 0x810000, resets with `--after watchdog_reset`).
6. **Verify:**
   - Serial (UART port, 115200): the factory app hands over to OTA0; then `Version: <v>`,
     `bootboots-version:<v>`, `[PROV] Provisioned as BootBootsThing (sandbox)`, Wi-Fi up, MQTT connected.
   - Settings survived: training mode / threshold / dry-run / Claude inference / camera settings
     are as before (logged at boot from NVS).
   - Fleet sees the device online; `get_version` returns `<v>`.
   - An OTA through Fleet to a published version newer than `<v>` completes and the device comes
     back on the new version, still provisioned.
7. **Rollback** if needed: `$ESPTOOL write_flash 0x110000 ota_0-old.bin` (the old app ignores prov
   and works with either table); `0x8000 partitions-old.bin` restores the old table; `0x9000 nvs.bin`
   restores the settings.
8. Delete the backup directory once the device is healthy (it holds the old key and Wi-Fi state).

If esptool can't connect, put the board in download mode: hold BOOT, plug into the OTG port,
release BOOT.
