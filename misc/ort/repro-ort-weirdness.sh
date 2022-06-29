#!/bin/bash

rm -rf nukeme
git init -b master nukeme
cd nukeme
git config commit.gpgsign false
git config user.name Tao
git config user.email tao@klerks.biz
touch firstfile
git add firstfile
git commit -m "First commit"
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
touch newworkingfile
git add newworkingfile
git commit -m "New working file"
git rm folder1/firstfile.txt
git add .
git commit -m "Deleted in working branch"
touch anothernewworkingfile
git add anothernewworkingfile
git commit -m "More regular history"


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

touch amasterfile
git add amasterfile
git commit -m "More regular history in master"

git checkout -b regularmerge featurebranch
git merge master -s recursive

git checkout -b ortmerge featurebranch
git merge master -s ort


# CONFLICT (rename/delete): folder1/firstfile.txt renamed to folder2/secondfile_renamed.txt in master, but deleted in HEAD.
#CONFLICT (modify/delete): folder2/secondfile_renamed.txt deleted in HEAD and modified in master.  Version master of folder2/secondfile_renamed.txt left in tree.
#100644 d1a2264e4ef2799ebaa9638d5224b3be00fb9f4e 1	folder2/secondfile_renamed.txt
#100644 a247ea3abeb9eee70797c7d0a78d50a937e2163f 3	folder2/secondfile_renamed.txt
