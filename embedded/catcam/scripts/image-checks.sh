#!/usr/bin/env bash
# BOOT-10: what a catcam firmware image must pass before it's published (publish-firmware.sh runs
# it; CI runs it too). Exits non-zero, saying why, on the first failure.
#
#   embedded/catcam/scripts/image-checks.sh <firmware.bin> <expected-version>
#
#   1. It's an ESP32 app image and says the expected version: the build's
#      "bootboots-version:<version>" tag (include/version.h), and no other version tag.
#   2. No PEM blocks (no "-----BEGIN ...-----" or "-----END ...-----" line). Not a bare
#      "-----BEGIN": mbedTLS's PEM parser has "-----BEGIN CERTIFICATE-----" and the
#      "-----END ... KEY-----" markers, NUL-terminated, in every image; a certificate or key
#      compiled in has its markers on lines of their own.
#   3. No per-device or per-environment strings: the Thing name, an IoT data or credentials
#      endpoint, the API host.
#   4. Neither the house Wi-Fi SSID nor its password, fetched from SSM now (WIFI_PROFILE, default
#      nakom.is-sandbox, where /Nakomis/Wifi/* lives) and handed to the scanner through its
#      environment: never echoed, never on a command line, never on disk.
#
# Needs python3 and the AWS CLI.
set -euo pipefail
umask 077

usage="usage: image-checks.sh <firmware.bin> <expected-version>"
bin=${1:?$usage}
version=${2:?$usage}
[ -f "$bin" ] || { echo "image-checks: no such file: $bin" >&2; exit 2; }
[[ "$version" =~ ^[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}(-[0-9A-Za-z.]+)?$ ]] || {
    echo "image-checks: version must be x.y.z (optionally -suffix), not '$version'" >&2
    exit 2
}

wifi_profile=${WIFI_PROFILE:-nakom.is-sandbox}
ssid=$(aws ssm get-parameter --profile "$wifi_profile" --name /Nakomis/Wifi/SSID \
    --query Parameter.Value --output text) || { echo "image-checks: can't read /Nakomis/Wifi/SSID (profile $wifi_profile)" >&2; exit 1; }
password=$(aws ssm get-parameter --profile "$wifi_profile" --name /Nakomis/Wifi/Password --with-decryption \
    --query Parameter.Value --output text) || { echo "image-checks: can't read /Nakomis/Wifi/Password (profile $wifi_profile)" >&2; exit 1; }
if [ -n "${GITHUB_ACTIONS:-}" ]; then
    echo "::add-mask::${ssid}"
    echo "::add-mask::${password}"
fi

BB_SSID=$ssid BB_PASSWORD=$password python3 - "$bin" "$version" <<'PY'
import os, re, sys

path, version = sys.argv[1], sys.argv[2]
image = open(path, "rb").read()
failures = []

# 1. An app image (ESP image magic) that says this version and no other.
if not image or image[0] != 0xE9:
    failures.append("not an ESP32 app image (no 0xE9 magic byte)")
tags = set(re.findall(rb"bootboots-version:([0-9A-Za-z.\-]+)\x00", image))
if tags != {version.encode()}:
    found = ", ".join(sorted(t.decode(errors="replace") for t in tags)) or "none"
    failures.append(f"the image's version tag is {found}, not {version}")

# 2. No PEM blocks: a BEGIN/END marker on a line of its own.
pem = re.search(rb"-----(BEGIN|END) [A-Z0-9 ]+-----\r?\n|\n-----(BEGIN|END) [A-Z0-9 ]+-----", image)
if pem:
    failures.append(f"the image contains a PEM block ({pem.group(0).strip().decode(errors='replace')}...)")

# 3. Per-device / per-environment strings (they live in the prov partition).
for needle in (b"BootBootsThing", b"-ats.iot.", b".credentials.iot.", b"api.bootboots."):
    if needle in image:
        failures.append(f"the image contains a per-device/per-environment string ({needle.decode()})")

# 4. The house Wi-Fi credentials: never print them.
for name in ("SSID", "PASSWORD"):
    value = os.environ.get(f"BB_{name}", "").encode()
    if not value:
        failures.append(f"no Wi-Fi {name.lower()} to check against")
    elif value in image:
        failures.append(f"the image contains the Wi-Fi {name.lower()}")

if failures:
    for f in failures:
        print(f"image-checks: FAIL: {f}", file=sys.stderr)
    sys.exit(1)
print(f"image-checks: OK: {os.path.basename(path)} is BootBoots {version}, {len(image)} bytes, "
      "no PEM blocks, no per-device strings, no Wi-Fi credentials")
PY
