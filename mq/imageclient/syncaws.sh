#!/bin/bash

set -e

export AWS_PROFILE=nakom.is-sandbox

pushd receivedimages
aws s3 sync . s3://bootbootstraining
popd

aws dynamodb scan --table-name catadata > catadata.json

for f in $(ls -b receivedimages/*.jpeg | xargs basename); do
    grep "$f" catadata.json > /dev/null || aws dynamodb put-item --table-name catadata --item "{\"imageName\":{\"S\":\""$f"\"}, \"uuid\":{\"S\": \"$(uuidgen)\"}}"
done;
