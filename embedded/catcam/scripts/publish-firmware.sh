#!/usr/bin/env bash
# BOOT-10: publish a checked catcam image for Fleet to offer.
#
#   embedded/catcam/scripts/publish-firmware.sh [--dry-run] <sandbox|prod> <firmware.bin> <x.y.z>
#
# Runs image-checks.sh first (version tag, no PEM blocks, no per-device strings, no Wi-Fi
# credentials), then uploads s3://nakomis-firmware-<env>/BootBoots/<x.y.z>/firmware.bin with its
# sha256 in the object metadata. The firmware bucket's Lambda adds it to BootBoots/manifest.json
# (this script never writes the manifest), which is what Fleet reads.
#
# A published version is never replaced: the upload is conditional (--if-none-match '*'), so it
# fails if the object exists, even against a publish racing this one. Re-publishing the same bytes
# is a no-op; other bytes under an existing version are refused.
#
# --dry-run runs every check and says what it would upload, but writes nothing to S3.
#
# AWS_PROFILE defaults to nakom.is-<env>; the Wi-Fi check reads SSM via WIFI_PROFILE (default
# nakom.is-sandbox). Needs python3 and the AWS CLI.
set -euo pipefail
umask 077

dry_run=false
args=()
for arg in "$@"; do
    case "$arg" in
        --dry-run) dry_run=true ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) args+=("$arg") ;;
    esac
done
usage="usage: publish-firmware.sh [--dry-run] <sandbox|prod> <firmware.bin> <x.y.z>"
[ "${#args[@]}" -eq 3 ] || { echo "$usage" >&2; exit 2; }
env=${args[0]} bin=${args[1]} version=${args[2]}
case "$env" in sandbox | prod) ;; *) echo "env must be sandbox or prod" >&2; exit 2 ;; esac
# Published versions are plain x.y.z (Fleet sorts them); dev builds aren't published.
[[ "$version" =~ ^[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}$ ]] || { echo "version must be x.y.z, not '$version'" >&2; exit 2; }
[ -f "$bin" ] || { echo "no such file: $bin" >&2; exit 2; }
export AWS_PROFILE=${AWS_PROFILE:-nakom.is-${env}}
export AWS_REGION=${AWS_REGION:-eu-west-2}

[ -z "${IMAGE_CHECKS_NO_AWS:-}" ] || { echo "IMAGE_CHECKS_NO_AWS is set: a publish must run every image check" >&2; exit 2; }

here=$(cd "$(dirname "$0")" && pwd)
"$here/image-checks.sh" "$bin" "$version"

bucket="nakomis-firmware-${env}"
object="BootBoots/${version}/firmware.bin"
sha=$(shasum -a 256 "$bin" | cut -d' ' -f1)
size=$(wc -c < "$bin" | tr -d ' ')

# Is this version already there? Only "not found" means go ahead: AccessDenied, expired
# credentials or a network failure stop here rather than being taken for "not published".
errfile=$(mktemp)
trap 'rm -f "$errfile"' EXIT
if existing=$(aws s3api head-object --bucket "$bucket" --key "$object" --query 'Metadata.sha256' --output text 2> "$errfile"); then
    if [ "$existing" = "$sha" ]; then
        echo "BootBoots ${version} is already published to ${env} with these bytes"
        exit 0
    fi
    echo "s3://${bucket}/${object} already exists with other bytes: a published version is never replaced" >&2
    exit 1
elif ! grep -q -E '\((404|NotFound|NoSuchKey)\)|Not Found' "$errfile"; then
    echo "can't tell whether s3://${bucket}/${object} exists (profile ${AWS_PROFILE}): not publishing" >&2
    sed 's/^/  /' "$errfile" >&2
    exit 1
fi

metadata="project=BootBoots,version=${version},sha256=${sha},size=${size}"
if $dry_run; then
    echo "dry run: would upload ${bin} (${size} bytes, sha256 ${sha}) to s3://${bucket}/${object} (profile ${AWS_PROFILE})"
    echo "dry run: aws s3api put-object --bucket ${bucket} --key ${object} --if-none-match '*' --content-type application/octet-stream --metadata ${metadata}"
    exit 0
fi

# --if-none-match: never replaced, even by a publish racing this one.
aws s3api put-object --bucket "$bucket" --key "$object" --body "$bin" --if-none-match '*' \
    --content-type application/octet-stream --metadata "$metadata" > /dev/null
echo "published BootBoots ${version} (sha256 ${sha}) to s3://${bucket}/${object}"
