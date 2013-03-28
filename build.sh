#!/bin/sh

gcc -std=gnu11 -Wall -Wextra -o bin/cowsync -g -O2 src/cowsync.c
exit $?
