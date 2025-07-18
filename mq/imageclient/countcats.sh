#!/bin/zsh

for c in "Mu" "Tau" "Chi" "Kappa" "Wolf" "NoCat"; do 
    echo -e "$c\t$(grep $c catadata.json | wc -l)"
done
