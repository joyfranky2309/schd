#!/usr/bin/env sh
set -eu

BUILD_DIR="${SCHD_BUILD_DIR:-build-sqlite}"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCHD_WITH_SQLITE=ON
cmake --build "$BUILD_DIR" --parallel 1

if [ "$#" -eq 0 ]; then
  exec "$BUILD_DIR/schd" run examples/pricing.json
fi

exec "$BUILD_DIR/schd" "$@"
