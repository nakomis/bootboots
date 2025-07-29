#!/bin/bash

set -e

# ssh mu "zsh -lc \"echo foo && aws\""
echo Syncing Mu to AWS
# ssh mu -f "zsh -lc 'screen -d -m $(cd ~/repos/nakomis/bootboots/mq/imageclient && AWS_PROFILE=nakom.is-sandbox ./syncaws.sh)'"

echo Copying Mu to local
for f in $(ssh mu "ls ~/repos/nakomis/bootboots/mq/imageclient/receivedimages/"); do 
    if [ ! -f receivedimages/$f ]; then
        scp mu:~/repos/nakomis/bootboots/mq/imageclient/receivedimages/$f ./receivedimages
        open ./receivedimages/$f
    fi
done

echo Getting catadata.json
rm -rf catadata.json
scp mu:/Users/martinmu_1/repos/nakomis/bootboots/mq/imageclient/catadata.json .

echo Opening new images

./open.sh

echo Done
exit 0
