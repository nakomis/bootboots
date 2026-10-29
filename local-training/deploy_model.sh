#!/usr/bin/env bash
# Ship a locally trained model to the BootBoots SageMaker endpoint (BOOT-9).
#
# Usage:
#   ./deploy_model.sh [--model-dir models_multiclass]
#
# Steps:
#   1. Export the .keras model to TF Serving SavedModel format + model.tar.gz
#   2. Upload model.tar.gz to s3://bootboots-models-<sandbox account>/local-models/<timestamp>/
#   3. Point infra/model.json's sandbox entry at it
#
# It makes no SageMaker calls: CDK owns the endpoint. Commit model.json and ship it through a PR
# (CI deploys sandbox on merge), or deploy by hand with:
#   cd infra && pnpm diff-sandbox BootBootsAiTrainingStack && pnpm deploy-stack-sandbox BootBootsAiTrainingStack
# The endpoint switches to the new model in place. To serve it in prod: infra/scripts/promote-model.sh prod

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL_JSON="$SCRIPT_DIR/../infra/model.json"
MODEL_DIR="models_multiclass"
PROFILE="${AWS_PROFILE:-nakom.is-sandbox}"
REGION="eu-west-2"

for arg in "$@"; do
    case "$arg" in
        --model-dir=*) MODEL_DIR="${arg#*=}" ;;
        --model-dir)   shift; MODEL_DIR="$1" ;;
    esac
done

echo "=== BootBoots Model Deploy ==="
echo "Model dir : $MODEL_DIR"
echo

# --- Export ---
cd "$SCRIPT_DIR"
source .venv/bin/activate

TARBALL="$SCRIPT_DIR/model.tar.gz"
python export_for_sagemaker.py --model-dir "$MODEL_DIR" --output "$TARBALL"
echo

# --- Upload to S3 ---
ACCOUNT=$(AWS_PROFILE="$PROFILE" aws sts get-caller-identity --query Account --output text --region "$REGION")
BUCKET="bootboots-models-$ACCOUNT"
LABEL=$(date -u +%Y%m%dT%H%M%S)
S3_KEY="local-models/$LABEL/model.tar.gz"

echo "Uploading to s3://$BUCKET/$S3_KEY ..."
AWS_PROFILE="$PROFILE" aws s3 cp "$TARBALL" "s3://$BUCKET/$S3_KEY" --region "$REGION"
echo

# --- model.json ---
python3 - "$MODEL_JSON" "$S3_KEY" "$LABEL" <<'EOF'
import json, sys
path, key, label = sys.argv[1:]
models = json.load(open(path))
models["sandbox"] = {"artefact": key, "label": label}
with open(path, "w") as f:
    json.dump(models, f, indent=2)
    f.write("\n")
EOF

echo "=== Uploaded; infra/model.json now names $LABEL for sandbox ==="
echo "Next: commit infra/model.json and open a PR (CI deploys sandbox on merge), or deploy by hand:"
echo "  cd infra && pnpm diff-sandbox BootBootsAiTrainingStack && pnpm deploy-stack-sandbox BootBootsAiTrainingStack"
echo "Then test with a signed POST to https://api.bootboots.sandbox.nakomis.com/infer"
