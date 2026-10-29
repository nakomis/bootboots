#!/usr/bin/env bash
# BOOT-10: write a catcam's provisioning data to its `prov` partition, so the (shared, published)
# firmware image needs no secrets and no environment of its own.
#
#   embedded/catcam/scripts/provision.sh <thing> <port> [env]          build and flash (default env sandbox)
#   embedded/catcam/scripts/provision.sh <thing> [env] --image-only OUT  build the image only, no device
#
# Builds an NVS image (namespace "prov", the keys lib/Provisioning reads) from:
#   thing          the Thing name argument (also the MQTT client id)
#   cert, key      SSM /BootsBoots/<thing>/certPem and /BootsBoots/<thing>/privKey (AWS_PROFILE)
#   iot_endpoint   aws iot describe-endpoint --endpoint-type iot:Data-ATS
#   cred_endpoint  aws iot describe-endpoint --endpoint-type iot:CredentialProvider
#   wifi_ssid/pass SSM /Nakomis/Wifi/SSID and /Nakomis/Wifi/Password (WIFI_PROFILE, default
#                  nakom.is-sandbox: they live only in the sandbox account)
#   api_host       API_HOST, default api.bootboots.sandbox.nakomis.com for sandbox (required otherwise)
#   role_alias     ROLE_ALIAS, default BootBootsRoleAlias
#   region, env    AWS_REGION (default eu-west-2), the env argument
#   root_ca        embedded/catcam/AmazonRootCA1.pem
# and flashes it at the prov partition's offset (from partitions_custom_s3.csv) with
# `esptool --after watchdog_reset`, after checking that the device's partition table has that prov
# partition (so it never writes into the old spiffs layout by mistake). Nothing else on the
# device is touched: nvs (settings, OTA flag, Wi-Fi/BLE state), the apps and otadata stay as they are.
#
# Secrets go only into a private `mktemp -d` directory that a trap removes, and into the
# generator through files there: never printed, never on a command line, never anywhere else on
# disk. --image-only OUT copies the finished image to OUT (mode 0600; it holds the private key and
# the Wi-Fi password: keep it private, delete it once flashed).
#
# AWS_PROFILE defaults to nakom.is-<env>. Needs the AWS CLI, PlatformIO (for esptool and its
# Python) and a Python with ESP-IDF's NVS partition generator (esp_idf_nvs_partition_gen; the IDF
# Python env under ~/.espressif has it, or set NVS_GEN_PYTHON).
set -euo pipefail
umask 077

image_only=
positional=()
while [ $# -gt 0 ]; do
    case "$1" in
        --image-only)
            [ $# -ge 2 ] || { echo "--image-only needs an output path" >&2; exit 2; }
            image_only=$2
            shift 2
            ;;
        -h | --help) sed -n '2,31p' "$0"; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) positional+=("$1"); shift ;;
    esac
done
usage="usage: provision.sh <thing> <port> [env]  |  provision.sh <thing> [env] --image-only OUT"
[ "${#positional[@]}" -ge 1 ] || { echo "$usage" >&2; exit 2; }
thing=${positional[0]}
port=
if [ -n "$image_only" ]; then
    # <thing> [env], tolerating a port in the usual place.
    rest=("${positional[@]:1}")
    if [ "${#rest[@]}" -gt 0 ] && [[ "${rest[0]}" == /dev/* ]]; then rest=("${rest[@]:1}"); fi
    [ "${#rest[@]}" -le 1 ] || { echo "$usage" >&2; exit 2; }
    env=${rest[0]:-sandbox}
else
    [ "${#positional[@]}" -ge 2 ] && [ "${#positional[@]}" -le 3 ] || { echo "$usage" >&2; exit 2; }
    port=${positional[1]}
    env=${positional[2]:-sandbox}
    [ -e "$port" ] || { echo "no such port: $port" >&2; exit 2; }
fi
case "$env" in sandbox | prod) ;; *) echo "env must be sandbox or prod" >&2; exit 2 ;; esac
[[ "$thing" =~ ^[A-Za-z0-9_:-]{1,128}$ ]] || { echo "not a valid Thing name: $thing" >&2; exit 2; }

export AWS_PROFILE=${AWS_PROFILE:-nakom.is-${env}}
export AWS_REGION=${AWS_REGION:-eu-west-2}
wifi_profile=${WIFI_PROFILE:-nakom.is-sandbox}
role_alias=${ROLE_ALIAS:-BootBootsRoleAlias}
if [ -z "${API_HOST:-}" ]; then
    [ "$env" = sandbox ] || { echo "set API_HOST for $env (only sandbox has a default)" >&2; exit 2; }
    API_HOST=api.bootboots.sandbox.nakomis.com
fi

here=$(cd "$(dirname "$0")" && pwd)
catcam=$(cd "$here/.." && pwd)
table="$catcam/partitions_custom_s3.csv"
root_ca="$catcam/AmazonRootCA1.pem"
prov_offset=$(awk -F', *' '$1 == "prov" { print $4 }' "$table")
prov_size=$(awk -F', *' '$1 == "prov" { print $5 }' "$table")
[ -n "$prov_offset" ] && [ -n "$prov_size" ] || { echo "no prov partition in $table" >&2; exit 1; }
[ -s "$root_ca" ] || { echo "missing $root_ca" >&2; exit 1; }

pio_python="$HOME/.platformio/penv/bin/python"
esptool="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
nvs_gen_shim="$HOME/.platformio/packages/framework-espidf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
nvs_tool="$HOME/.platformio/packages/framework-espidf/components/nvs_flash/nvs_partition_tool/nvs_tool.py"
gen_part="$HOME/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py"

# A Python that has the NVS partition generator (the PlatformIO framework-espidf shim just runs
# `python -m esp_idf_nvs_partition_gen`, so the module is what matters).
nvs_python=
candidates=("${NVS_GEN_PYTHON:-}")
idf_pythons=("$HOME"/.espressif/python_env/idf*/bin/python)
for ((i = ${#idf_pythons[@]} - 1; i >= 0; i--)); do candidates+=("${idf_pythons[i]}"); done   # newest IDF first
candidates+=(python3)
for candidate in "${candidates[@]}"; do
    [ -n "$candidate" ] && [ -x "$candidate" ] || command -v "$candidate" > /dev/null 2>&1 || continue
    if "$candidate" -c 'import esp_idf_nvs_partition_gen' 2> /dev/null; then nvs_python=$candidate; break; fi
done
[ -n "$nvs_python" ] || {
    echo "no Python with esp_idf_nvs_partition_gen: pip install esp-idf-nvs-partition-gen, or set NVS_GEN_PYTHON" >&2
    exit 1
}

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# Secrets straight into the private directory, never through argv or stdout.
aws ssm get-parameter --name "/BootsBoots/${thing}/certPem" --with-decryption \
    --query Parameter.Value --output text > "$dir/cert"
aws ssm get-parameter --name "/BootsBoots/${thing}/privKey" --with-decryption \
    --query Parameter.Value --output text > "$dir/key"
aws ssm get-parameter --profile "$wifi_profile" --name /Nakomis/Wifi/SSID \
    --query Parameter.Value --output text > "$dir/wifi_ssid"
aws ssm get-parameter --profile "$wifi_profile" --name /Nakomis/Wifi/Password --with-decryption \
    --query Parameter.Value --output text > "$dir/wifi_pass"
aws iot describe-endpoint --endpoint-type iot:Data-ATS --query endpointAddress --output text > "$dir/iot_endpoint"
aws iot describe-endpoint --endpoint-type iot:CredentialProvider --query endpointAddress --output text > "$dir/cred_endpoint"
printf '%s' "$API_HOST" > "$dir/api_host"
printf '%s' "$role_alias" > "$dir/role_alias"
printf '%s' "$AWS_REGION" > "$dir/region"
printf '%s' "$env" > "$dir/env"
printf '%s' "$thing" > "$dir/thing"
cp "$root_ca" "$dir/root_ca"

# Normalise (the CLI's text output ends in a newline), check the sizes against what NVS and
# ESP-IDF's Wi-Fi accept, and write the generator's CSV: every value as a file, so nothing secret
# is in the CSV itself or needs quoting.
python3 - "$dir" <<'PY'
import csv, os, sys

d = sys.argv[1]
single_line = ["thing", "wifi_ssid", "wifi_pass", "iot_endpoint", "cred_endpoint", "api_host",
               "role_alias", "region", "env"]
pem = {"root_ca": "CERTIFICATE", "cert": "CERTIFICATE", "key": "PRIVATE KEY"}
limits = {"wifi_ssid": 32, "wifi_pass": 63}

for k in single_line:
    p = os.path.join(d, k)
    v = open(p, encoding="utf-8").read().rstrip("\r\n")
    if not v or v == "None":
        sys.exit(f"provision: {k} is empty")
    if len(v.encode()) > limits.get(k, 255):
        sys.exit(f"provision: {k} is too long ({len(v.encode())} bytes > {limits.get(k, 255)})")
    open(p, "w", encoding="utf-8").write(v)
for k, kind in pem.items():
    p = os.path.join(d, k)
    v = open(p, encoding="utf-8").read().strip() + "\n"
    if "-----BEGIN" not in v or kind not in v:
        sys.exit(f"provision: {k} doesn't look like a PEM {kind.lower()}")
    if len(v.encode()) >= 4000:  # an NVS string must fit one 4 KB page
        sys.exit(f"provision: {k} is too big for an NVS string ({len(v.encode())} bytes)")
    open(p, "w", encoding="utf-8").write(v)

with open(os.path.join(d, "prov.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(("key", "type", "encoding", "value"))
    w.writerow(("prov", "namespace", "", ""))
    for k in single_line + list(pem):
        w.writerow((k, "file", "string", os.path.join(d, k)))
PY

if [ -f "$nvs_gen_shim" ]; then
    "$nvs_python" "$nvs_gen_shim" generate "$dir/prov.csv" "$dir/prov.bin" "$prov_size" > /dev/null
else
    "$nvs_python" -m esp_idf_nvs_partition_gen generate "$dir/prov.csv" "$dir/prov.bin" "$prov_size" > /dev/null
fi

# Read the image back with ESP-IDF's NVS parser: right size, passes its integrity check (CRCs,
# a free page), and every key is in the "prov" namespace. Prints key names and sizes, never values.
"$pio_python" - "$dir/prov.bin" "$prov_size" "$(dirname "$nvs_tool")" <<'PY'
import contextlib, io, os, sys

image, size, tool_dir = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
actual = os.path.getsize(image)
if actual != size:
    sys.exit(f"provision: the image is {actual} bytes, not the partition's {size}")
want = {"thing", "wifi_ssid", "wifi_pass", "iot_endpoint", "cred_endpoint", "api_host", "role_alias",
        "region", "env", "root_ca", "cert", "key"}
sys.path.insert(0, tool_dir)
import nvs_check, nvs_logger, nvs_parser  # noqa: E402

nvs = nvs_parser.NVS_Partition(os.path.basename(image), bytearray(open(image, "rb").read()))
log = nvs_logger.NVS_Logger(color="always")
report = io.StringIO()
with contextlib.redirect_stdout(report):
    nvs_check.integrity_check(nvs, log)
if nvs_logger.NVS_Logger.ansi["red"] in report.getvalue() or nvs_logger.NVS_Logger.ansi["yellow"] in report.getvalue():
    sys.exit("provision: the image fails ESP-IDF's NVS integrity check")

namespaces, sizes = {}, {}
for page in nvs.pages:
    for entry in page.entries:
        if entry.state == "Written" and entry.metadata["namespace"] == 0:
            namespaces[entry.data["value"]] = entry.key
for page in nvs.pages:
    for entry in page.entries:
        if entry.state == "Written" and entry.metadata["namespace"] != 0 and entry.metadata["type"] == "string":
            if namespaces.get(entry.metadata["namespace"]) == "prov":
                sizes[entry.key] = entry.data["size"]
missing = want - set(sizes)
if missing:
    sys.exit(f"provision: the image lacks {', '.join(sorted(missing))}")
print(f"provision: image OK: {actual} bytes, namespace 'prov', keys {', '.join(sorted(sizes))}")
PY

if [ -n "$image_only" ]; then
    cp "$dir/prov.bin" "$image_only"
    chmod 600 "$image_only"
    echo "wrote the prov image for ${thing} (${env}) to ${image_only}: it holds the private key and Wi-Fi password; delete it once flashed"
    exit 0
fi

# The device must already have the BOOT-10 partition table, or this would land in spiffs.
"$pio_python" "$esptool" --chip esp32s3 --port "$port" --after no_reset \
    read_flash 0x8000 0xc00 "$dir/table.bin" > /dev/null
"$pio_python" "$gen_part" --quiet "$dir/table.bin" "$dir/table.csv"
awk -F, -v off="$prov_offset" '$1 == "prov" && $3 == "nvs" && tolower($4) == tolower(off) { found = 1 } END { exit !found }' "$dir/table.csv" || {
    echo "the device's partition table has no prov partition at ${prov_offset}: flash the BOOT-10 partition table first (docs/provisioning.md)" >&2
    exit 1
}

"$pio_python" "$esptool" --chip esp32s3 --port "$port" --baud 921600 --after watchdog_reset \
    write_flash "$prov_offset" "$dir/prov.bin"
echo "provisioned ${thing} (${env}) at ${prov_offset}"
