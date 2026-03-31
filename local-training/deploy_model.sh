#!/usr/bin/env bash
# Deploy a locally trained model to the BootBoots SageMaker endpoint.
#
# Usage:
#   ./deploy_model.sh [--model-dir models_multiclass]
#
# Steps:
#   1. Export the .keras model to TF Serving SavedModel format + model.tar.gz
#   2. Upload model.tar.gz to S3
#   3. Create SageMaker Model + EndpointConfig with timestamped names
#   4. Update the 'bootboots' endpoint to the new config

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL_DIR="models_multiclass"

for arg in "$@"; do
    case "$arg" in
        --model-dir=*) MODEL_DIR="${arg#*=}" ;;
        --model-dir)   shift; MODEL_DIR="$1" ;;
    esac
done

REGION="eu-west-2"
ENDPOINT_NAME="bootboots"
SAGEMAKER_ROLE="arn:aws:iam::975050268859:role/BootBootsSageMakerRole"
TF_SERVING_IMAGE="763104351884.dkr.ecr.${REGION}.amazonaws.com/tensorflow-inference:2.16-cpu"

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
ACCOUNT=$(AWS_PROFILE=nakom.is-sandbox aws sts get-caller-identity \
    --query Account --output text --region "$REGION")
BUCKET="bootboots-models-$ACCOUNT"
TIMESTAMP=$(date -u +%Y%m%dT%H%M%S)
S3_KEY="local-models/$TIMESTAMP/model.tar.gz"
S3_URL="s3://$BUCKET/$S3_KEY"

echo "Uploading to $S3_URL ..."
AWS_PROFILE=nakom.is-sandbox aws s3 cp "$TARBALL" "$S3_URL" --region "$REGION"
echo

# --- Create SageMaker Model ---
MODEL_NAME="bootboots-local-$TIMESTAMP"
echo "Creating SageMaker model: $MODEL_NAME ..."
AWS_PROFILE=nakom.is-sandbox aws sagemaker create-model \
    --region "$REGION" \
    --model-name "$MODEL_NAME" \
    --execution-role-arn "$SAGEMAKER_ROLE" \
    --primary-container \
        "Image=$TF_SERVING_IMAGE,ModelDataUrl=$S3_URL"
echo

# --- Create Endpoint Config ---
CONFIG_NAME="bootboots-local-config-$TIMESTAMP"
echo "Creating endpoint config: $CONFIG_NAME ..."
AWS_PROFILE=nakom.is-sandbox aws sagemaker create-endpoint-config \
    --region "$REGION" \
    --endpoint-config-name "$CONFIG_NAME" \
    --production-variants \
        "VariantName=AllTraffic,ModelName=$MODEL_NAME,ServerlessConfig={MemorySizeInMB=2048,MaxConcurrency=5}"
echo

# --- Update Endpoint ---
echo "Updating endpoint '$ENDPOINT_NAME' to use new config ..."
AWS_PROFILE=nakom.is-sandbox aws sagemaker update-endpoint \
    --region "$REGION" \
    --endpoint-name "$ENDPOINT_NAME" \
    --endpoint-config-name "$CONFIG_NAME"

echo
echo "Waiting for endpoint to be InService (this can take a few minutes) ..."
AWS_PROFILE=nakom.is-sandbox aws sagemaker wait endpoint-in-service \
    --region "$REGION" \
    --endpoint-name "$ENDPOINT_NAME"

echo
echo "=== Deploy complete ==="
echo "Model    : $MODEL_NAME"
echo "Config   : $CONFIG_NAME"
echo "Endpoint : $ENDPOINT_NAME (InService)"
echo
echo "Test with:"
echo "  curl -X POST -H 'Content-Type: image/jpeg' --data-binary @photo.jpg \\"
echo "    https://api.bootboots.sandbox.nakomis.com/infer"
