#!/bin/bash

cloc . --by-file --exclude-dir=.pio,ignored,.vscode,catcam-images,node_modules,cdk.out,.claude,dist --not-match-f="package-lock"
# cloc . --exclude-dir=.pio,ignored,.vscode,catcam-images,node_modules,cdk.out,.claude,dist --not-match-f="package-lock"
