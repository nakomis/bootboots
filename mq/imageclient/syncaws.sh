#!/bin/bash

set -e

export AWS_PROFILE=nakom.is-sandbox

pushd receivedimages > /dev/null
aws s3 sync . s3://bootbootstraining
popd > /dev/null

aws dynamodb scan --table-name catadata > catadata.json

for f in $(ls -b receivedimages/*.jpeg | xargs basename); do
    grep "$f" catadata.json > /dev/null || aws dynamodb put-item --table-name catadata --item "{\"imageName\":{\"S\":\""$f"\"}, \"uuid\":{\"S\": \"$(uuidgen)\"}}"
done;

# release any claimed items that have not been processed
for ff in $(jq '. | .Items.[] | select((.user != null) and (.cat == null)) | .imageName.S + ":" + .uuid.S' catadata.json | tr -d '"'); do
    IFS=: read -r FILENAME UUID <<< "$ff"; aws dynamodb update-item --table-name catadata --key "{\"imageName\":{\"S\":\"$FILENAME\"}, \"uuid\":{\"S\": \"$UUID\"}}" --update-expression "REMOVE cat, claimedAt, #user, reviewedAt" --expression-attribute-names "{\"#user\":\"user\"}"
    # aws dynamodb update-item --table-name catadata --key "{\"imageName\":{\"S\":\""$FILENAME"\"}, \"uuid\":{\"S\": \"$UUID\"}}";
done;
