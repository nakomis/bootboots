#!/usr/bin/env bash
# Publish the BootBoots panel build to an environment's nakomis-shell bucket.
#
#   scripts/publish-web.sh [--dry-run] <env> <dist-dir>
#
# Generates config.json from SSM parameters for <env>, syncs <dist-dir> to
# s3://<shell bucket>/remotes/bootboots/ (config.json excluded), uploads config.json with
# Cache-Control: no-cache, then invalidates /remotes/bootboots/* on the shell's distribution.
#
# Uses the ambient AWS credentials (CI sets them; locally set AWS_PROFILE). The release version comes from
# RELEASE_VERSION (default 0.0.0-dev). Everything environment-specific comes from SSM:
#   /nakomis-shell/<env>/bucket-name, /nakomis-shell/<env>/distribution-id
#   /nakomis-fleet/<env>/api-url, /nakomis-fleet/<env>/ws-url
#   /nakomis-bootboots/<env>/{identity-pool-id,images-bucket,catadata-table,events-api-url,catcam-thing}
#   /nakomis-bootboots/<env>/sam-server-url (optional)
set -euo pipefail

usage() {
  echo "Usage: $0 [--dry-run] <env> <dist-dir>" >&2
  exit 2
}

dry_run=false
args=()
for arg in "$@"; do
  case "$arg" in
    --dry-run) dry_run=true ;;
    -h | --help) usage ;;
    *) args+=("$arg") ;;
  esac
done
[[ ${#args[@]} -eq 2 ]] || usage
env_name="${args[0]}"
dist_dir="${args[1]}"
version="${RELEASE_VERSION:-0.0.0-dev}"

command -v aws > /dev/null || { echo "aws CLI not found" >&2; exit 1; }
command -v jq > /dev/null || { echo "jq not found" >&2; exit 1; }
[[ -d "$dist_dir" ]] || { echo "dist directory not found: $dist_dir" >&2; exit 1; }
[[ -f "$dist_dir/remoteEntry.js" ]] || { echo "$dist_dir/remoteEntry.js not found: is this a build?" >&2; exit 1; }

region="${AWS_REGION:-${AWS_DEFAULT_REGION:-$(aws configure get region || true)}}"
[[ -n "$region" ]] || { echo "No AWS region: set AWS_REGION or configure one" >&2; exit 1; }

missing=()

# ssm <variable> <name> <required|optional>: sets <variable> to the parameter's value (empty if absent) and
# records required parameters that do not exist. Not run in a subshell, so `missing` is kept.
ssm() {
  local var="$1" name="$2" kind="$3" value
  value="$(aws ssm get-parameters --names "$name" --query 'Parameters[0].Value' --output text)"
  if [[ "$value" == "None" || -z "$value" ]]; then
    [[ "$kind" == "optional" ]] || missing+=("$name")
    value=""
  fi
  printf -v "$var" '%s' "$value"
}

bb="/nakomis-bootboots/$env_name"
ssm shell_bucket "/nakomis-shell/$env_name/bucket-name" required
ssm distribution_id "/nakomis-shell/$env_name/distribution-id" required
ssm identity_pool_id "$bb/identity-pool-id" required
ssm images_bucket "$bb/images-bucket" required
ssm catadata_table "$bb/catadata-table" required
ssm events_api_url "$bb/events-api-url" required
ssm catcam_thing "$bb/catcam-thing" required
ssm fleet_api_url "/nakomis-fleet/$env_name/api-url" required
ssm fleet_ws_url "/nakomis-fleet/$env_name/ws-url" required
ssm sam_server_url "$bb/sam-server-url" optional

if [[ ${#missing[@]} -gt 0 ]]; then
  echo "Missing required SSM parameters for environment '$env_name':" >&2
  printf '  %s\n' "${missing[@]}" >&2
  exit 1
fi

config="$(jq -n \
  --arg environment "$env_name" \
  --arg version "$version" \
  --arg region "$region" \
  --arg identityPoolId "$identity_pool_id" \
  --arg imagesBucket "$images_bucket" \
  --arg catadataTable "$catadata_table" \
  --arg eventsApiUrl "$events_api_url" \
  --arg fleetApiUrl "$fleet_api_url" \
  --arg fleetWsUrl "$fleet_ws_url" \
  --arg catcamThing "$catcam_thing" \
  --arg samServerUrl "$sam_server_url" \
  '{environment: $environment, version: $version, region: $region, identityPoolId: $identityPoolId,
    imagesBucket: $imagesBucket, catadataTable: $catadataTable, eventsApiUrl: $eventsApiUrl,
    fleetApiUrl: $fleetApiUrl, fleetWsUrl: $fleetWsUrl, catcamThing: $catcamThing, project: "bootboots",
    samServerUrl: $samServerUrl}')"

dest="s3://$shell_bucket/remotes/bootboots"
config_file="$(mktemp)"
trap 'rm -f "$config_file"' EXIT
printf '%s\n' "$config" > "$config_file"

sync_cmd=(aws s3 sync "$dist_dir" "$dest/" --delete --exclude config.json)
put_cmd=(aws s3 cp "$config_file" "$dest/config.json" --cache-control no-cache --content-type application/json)
invalidate_cmd=(aws cloudfront create-invalidation --distribution-id "$distribution_id" --paths '/remotes/bootboots/*')

if $dry_run; then
  echo "config.json:"
  echo "$config"
  echo
  echo "Dry run, would run:"
  printf "  %q " "${sync_cmd[@]}"
  echo
  printf '  %q ' "${put_cmd[@]}"
  echo
  printf '  %q ' "${invalidate_cmd[@]}"
  echo
  exit 0
fi

"${sync_cmd[@]}"
"${put_cmd[@]}"
"${invalidate_cmd[@]}" > /dev/null
echo "Published BootBoots $version to $dest and invalidated /remotes/bootboots/*"
