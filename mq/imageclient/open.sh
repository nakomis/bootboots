#!/bin/bash

open $(ls -btr receivedimages/*.jpeg | grep -v \.sh | tail -n 1)
