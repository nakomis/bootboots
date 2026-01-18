#!/usr/bin/env bash

set -euo pipefail

#!/bin/bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
IMAGES_DIR=$SCRIPT_DIR/../catcam-images

if [ "$#" -lt 1 ]; then
  echo "Usage:"
  echo "  $0 Name [Name ...]"
  echo "  $0 - Name [Name ...]"
  exit 1
fi

NEGATIVE=false

if [ "$1" = "-" ]; then
  NEGATIVE=true
  shift
fi

if [ "$#" -eq 0 ]; then
  echo "Error: No names provided"
  exit 1
fi

# Build regex: Tau|Mu|Kappa
REGEX=$(printf '|"%s"' "$@")
REGEX=${REGEX:1}               # strip leading |
REGEX="\"name\"[[:space:]]*:[[:space:]]*(${REGEX})"

for txt in $IMAGES_DIR/*.txt; do
  jpg="${txt%.txt}.jpg"
  [ -f "$jpg" ] || continue

  if $NEGATIVE; then
    if ! grep -Eq "$REGEX" "$txt"; then
      echo "$jpg"
    fi
  else
    if grep -Eq "$REGEX" "$txt"; then
      echo "$jpg"
    fi
  fi
done
