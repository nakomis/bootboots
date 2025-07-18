#!/bin/bash

set -e

for f in $(ssh mu "ls ~/repos/nakomis/bootboots/mq/imageclient/receivedimages/"); do 
    if [ ! -f receivedimages/$f ]; then
        scp mu:~/repos/nakomis/bootboots/mq/imageclient/receivedimages/$f ./receivedimages
        open ./receivedimages/$f
    fi
done

rm -rf catadata.json
scp mu:/Users/martinmu_1/repos/nakomis/bootboots/mq/imageclient/catadata.json .

./open.sh
