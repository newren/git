#!/bin/bash

for hash in a56ae56f99 2a86b4031b 7c91befd57 97ceae39ae; do
    cd ../git
    git checkout $hash
    build
    cd ../linux-stable
    ../git/do-timings
done
