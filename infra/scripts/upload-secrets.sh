#!/usr/bin/env bash
# Uploads secrets from infra/secrets.json to AWS SSM Parameter Store.
# Run once before the first CDK deploy, and again whenever secrets change.
#
# Usage:
#   export AWS_PROFILE=nakom.is-sandbox
#   ./scripts/upload-secrets.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECRETS_FILE="$SCRIPT_DIR/../secrets.json"
REGION="eu-west-2"

if [ ! -f "$SECRETS_FILE" ]; then
  echo "Error: secrets.json not found at $SECRETS_FILE"
  echo "Copy secrets.json.template to secrets.json and fill in your values."
  exit 1
fi

ANTHROPIC_KEY=$(python3 -c "import json,sys; d=json.load(open('$SECRETS_FILE')); print(d['anthropicApiKey'])")

if [ -z "$ANTHROPIC_KEY" ] || [ "$ANTHROPIC_KEY" = "YOUR_ANTHROPIC_API_KEY_HERE" ]; then
  echo "Error: anthropicApiKey is not set in secrets.json"
  exit 1
fi

echo "Uploading Anthropic API key to SSM..."
aws ssm put-parameter \
  --name "/bootboots/anthropic-api-key" \
  --value "$ANTHROPIC_KEY" \
  --type SecureString \
  --region "$REGION" \
  --overwrite \
  --no-cli-pager

echo "Done. You can now run: AWS_PROFILE=nakom.is-sandbox npx cdk deploy BootBootsApiGatewayStack"
