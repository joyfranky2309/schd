#!/usr/bin/env sh
set -e

cmake -S . -B build -G Ninja
cmake --build build
./build/schd