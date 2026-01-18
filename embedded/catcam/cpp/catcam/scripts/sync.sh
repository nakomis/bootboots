#!/bin/bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p $SCRIPT_DIR/../catcam-images
AWS_PROFILE=nakom.is-sandbox aws s3 sync s3://bootboots-images-975050268859-eu-west-2/catcam-images/ $SCRIPT_DIR/../catcam-images
