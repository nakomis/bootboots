#!/bin/bash

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <input-image>"
    exit 1
fi

INPUT="$1"
BASENAME="${INPUT%.*}"
OUTPUT="${BASENAME}.svg"

# Create temp directory for intermediates (portable across Linux/macOS)
TEMP_DIR=$(mktemp -d)
TEMP_PNG="$TEMP_DIR/temp.png"
TEMP_PBM="$TEMP_DIR/temp.pbm"

# Clean up temp directory on exit
trap 'rm -rf "$TEMP_DIR"' EXIT

# Convert to high-contrast monochrome PNG first
convert "$INPUT" -colorspace Gray -threshold 50% -negate "$TEMP_PNG"

# For better results with logos/graphics:
# convert "$INPUT" -colorspace Gray -contrast-stretch 0 -threshold 50% "$TEMP_PNG"

# Adjust the threshold percentage (50%) of the above to get the right balance of black/white for your image.

# First convert to PBM (required by potrace)
convert "$TEMP_PNG" "$TEMP_PBM"

# Then trace to SVG
potrace "$TEMP_PBM" -s -W 10mm -o "$OUTPUT"

echo "Created: $OUTPUT"

# Using -W alone preserves the aspect ratio.
# To force a 'sqaush' use the following
# potrace output.pbm -s -W 10mm -H 10mm -o output.svg 

