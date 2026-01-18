#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${(%):-%N}")" && pwd)"
IMAGES_DIR=$SCRIPT_DIR/../catcam-images

mkdir -p $IMAGES_DIR/tagged

for txt in $IMAGES_DIR/*.txt; do
    jpg="${txt%.txt}.jpg"
    [ -f "$jpg" ] || continue

    filename=$(basename $jpg)
    txtname=$(basename $txt)

    [ -f "$IMAGES_DIR/tagged/$filename" ] && continue

    likely=$(jq '.mostLikelyCat.name' catcam-images/$txtname | tr -d '"')
    confidence=$(jq '.mostLikelyCat.confidence' catcam-images/$txtname)
    text=$(echo $((100 * $confidence)) | bc -l | xargs printf "%.2f%%\n")
    magick catcam-images/$filename \( -background black -fill white -font Arial -pointsize 36 label:"$likely - $text" \) -gravity northwest -geometry +10+10 -composite catcam-images/tagged/$filename
    
done