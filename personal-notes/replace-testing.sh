#!/bin/bash

set -e

rm -rf whatever copy

git init -q -b main whatever
cd whatever/
>empty
git add empty
git commit -q -m initial

git clone -q . ../copy

git replace -f deadbeefdeadbeefdeadbeefdeadbeefdeadbeef $(git rev-parse main)

git branch foobar deadbeefdeadbeefdeadbeefdeadbeefdeadbeef
git checkout -q foobar
echo stuff >empty
git add empty
git commit -q -m more

# Push to repo w/o replace ref
#git push -f ../copy foobar:main

# Push to repo w/ replace ref
#git -C ../copy replace -f deadbeefdeadbeefdeadbeefdeadbeefdeadbeef $(git rev-parse main)
#git push -f ../copy foobar:main # Push to repo w/ replace ref

# Fetch into repo w/o replace ref
#git -C ../copy fetch ../whatever foobar

# Fetch into repo w/ replace ref
git -C ../copy replace -f deadbeefdeadbeefdeadbeefdeadbeefdeadbeef $(git rev-parse main)
git -C ../copy fetch ../whatever foobar
