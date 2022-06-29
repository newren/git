#!/bin/bash

set -eux

test -d nukeme-dscho && die "Remove nukeme-dscho first"
git init -b main nukeme-dscho
cd nukeme-dscho

cat >main.c <<EOF
int core(void) {
    printf("Hello, world!\n");
}
EOF
git add main.c
git commit -m Initial

git branch upstream
git branch local-rename-to-hi
git branch local-add-caller-of-core

git checkout local-rename-to-hi

cat >main.c <<EOF
int hi(void) {
    printf("Hello, world!\n");
}
EOF
git add main.c
git commit -m renamed

git checkout local-add-caller-of-core

cat >main.c <<EOF
int core(void) {
    printf("Hello, world!\n");
}
/* caller */
void caller(void) {
    core();
}
EOF
git add main.c
git commit -m added-caller

git checkout -b merged local-rename-to-hi
git merge --no-edit local-add-caller-of-core
cat >main.c <<EOF
int hi(void) {
    printf("Hello, world!\n");
}
/* caller */
void caller(void) {
    hi();
}
EOF
git add main.c
git commit -C HEAD --amend

git checkout upstream

cat >main.c <<EOF
int greeting(void) {
    printf("Hello, world!\n");
}
/* main event loop */
void event_loop(void) {
    /* TODO: place holder for now */
}
EOF
git add main.c
git commit -m rename-and-add-event-loop


git rebase upstream local-rename-to-hi
cat >main.c <<EOF
int hi(void) {
    printf("Hello, world!\n");
}
/* main event loop */
void event_loop(void) {
    /* TODO: place holder for now */
}
EOF
git add main.c
git rebase --continue


git rebase upstream local-add-caller-of-core
cat >main.c <<EOF
int greeting(void) {
    printf("Hello, world!\n");
}
/* main event loop */
void event_loop(void) {
    /* TODO: place holder for now */
}
/* caller */
void caller(void) {
    core();
}
EOF
git add main.c
git rebase --continue

git checkout -b new-merge local-rename-to-hi
git merge local-add-caller-of-core

