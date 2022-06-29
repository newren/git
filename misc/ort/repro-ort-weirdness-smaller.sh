#!/bin/bash

rm -rf nukeme
git init -b master nukeme
cd nukeme
mkdir folder1
mkdir folder2

cat >> folder1/firstfile.txt << "EOF"
FirstLine
Second Line
Line Number 3
Fourth Line
There can Always be more Lines

That was an Empty Line
Some Unique Stuff
But Not too Much
EOF

cat >> folder2/secondfile.txt << "EOF"
FirstLine
Second Line
Line Number 3
There can Always be more Lines
But not too many!

That was an Empty Line
Otherwise Unique Stuff
But Not too Much
EOF

git add .
git commit -m "Starting State"

git checkout -b featurebranch
#git rm folder1/firstfile.txt
#git rm folder2/secondfile.txt
echo bleh >>folder1/firstfile.txt
git add .
git commit -m "Deleted in working branch"


git checkout master
rm folder1/firstfile.txt
git add .
git commit -m "also deleted here"

rm folder2/secondfile.txt

cat >> folder2/secondfile_renamed.txt << "EOF"
FirstLine
Second Line
Line Number 3
There can Always be more Lines
But not too many!

That was an Empty Line
Otherwise Almost Unique Stuff
But Not too Much
EOF

git add .
git commit -m "a high-percentage rename"

git checkout -b regularmerge featurebranch
git merge --no-edit master -s recursive
git reset --hard

git checkout -b ortmerge featurebranch
git merge --no-edit master -s ort

git log --all --graph --oneline --name-status

# CONFLICT (rename/delete): folder1/firstfile.txt renamed to folder2/secondfile_renamed.txt in master, but deleted in HEAD.
#CONFLICT (modify/delete): folder2/secondfile_renamed.txt deleted in HEAD and modified in master.  Version master of folder2/secondfile_renamed.txt left in tree.
#100644 d1a2264e4ef2799ebaa9638d5224b3be00fb9f4e 1	folder2/secondfile_renamed.txt
#100644 a247ea3abeb9eee70797c7d0a78d50a937e2163f 3	folder2/secondfile_renamed.txt
