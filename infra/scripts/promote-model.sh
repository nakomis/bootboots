#!/usr/bin/env bash
# Promote the model sandbox serves to another environment (BOOT-9, BOOT-12).
#
# Usage: infra/scripts/promote-model.sh <env>
#
# Copies the artefact named by infra/model.json's sandbox entry from sandbox's
# bootboots-models-<account> bucket into <env>'s, at the same key, then writes the same entry for
# <env> into model.json. A key already present in the target is never overwritten. Training stays in
# sandbox; other environments only ever get a model promoted to them.
#
# Profiles: SOURCE_PROFILE (default nakom.is-sandbox) and TARGET_PROFILE (default nakom.is-<env>).
# Needs AWS CLI v2.22+ (put-object --if-none-match).
# Then commit model.json and deploy BootBootsAiTrainingStack to <env>.

set -euo pipefail

ENV="${1:?usage: promote-model.sh prod}"
# Sandbox is the source (training only happens there); prod is the only other environment.
[ "$ENV" = prod ] || { echo "promote-model.sh: <env> must be prod" >&2; exit 1; }
SOURCE_PROFILE="${SOURCE_PROFILE:-nakom.is-sandbox}"
TARGET_PROFILE="${TARGET_PROFILE:-nakom.is-$ENV}"
REGION="${AWS_REGION:-eu-west-2}"
MODEL_JSON="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/model.json"

read -r KEY LABEL < <(python3 -c '
import json, sys
s = json.load(open(sys.argv[1]))["sandbox"]
print(s["artefact"], s["label"])' "$MODEL_JSON")

account() { AWS_PROFILE="$1" aws sts get-caller-identity --query Account --output text; }
sha256() { if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
SRC="bootboots-models-$(account "$SOURCE_PROFILE")"
DST="bootboots-models-$(account "$TARGET_PROFILE")"
[ "$SRC" != "$DST" ] || { echo "source and target are the same account" >&2; exit 1; }

echo "Promoting $LABEL: s3://$SRC/$KEY -> s3://$DST/$KEY"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
AWS_PROFILE="$SOURCE_PROFILE" aws s3 cp "s3://$SRC/$KEY" "$TMP/model.tar.gz" --region "$REGION" --only-show-errors
SHA=$(sha256 "$TMP/model.tar.gz")
EXISTING=$(AWS_PROFILE="$TARGET_PROFILE" aws s3api head-object --bucket "$DST" --key "$KEY" --region "$REGION" \
  --query 'Metadata.sha256' --output text 2> /dev/null || true)
if [ -n "$EXISTING" ]; then
  # Only a promotion of the very same bytes counts: never point prod at a different file.
  [ "$EXISTING" = "$SHA" ] || { echo "s3://$DST/$KEY exists with sha256 '$EXISTING', not $SHA: refusing" >&2; exit 1; }
  echo "Already in $DST (same sha256); not copying."
else
  AWS_PROFILE="$TARGET_PROFILE" aws s3api put-object --bucket "$DST" --key "$KEY" --body "$TMP/model.tar.gz" \
    --if-none-match '*' --metadata "sha256=$SHA,promoted-from=$SRC" --region "$REGION" > /dev/null
  echo "Copied ($SHA)."
fi

python3 -c '
import json, sys
path, env = sys.argv[1:]
models = json.load(open(path))
models[env] = dict(models["sandbox"])
with open(path, "w") as f:
    json.dump(models, f, indent=2)
    f.write("\n")' "$MODEL_JSON" "$ENV"

echo "model.json: $ENV now serves $LABEL. Commit it, then: cd infra && pnpm diff-$ENV BootBootsAiTrainingStack"
