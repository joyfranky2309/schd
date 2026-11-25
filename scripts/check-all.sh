#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

presets=(
  release
  asan
  tsan
  quantlib
  release-lowmem
  asan-lowmem
  tsan-lowmem
  quantlib-lowmem
)

overall_status=0
for preset in "${presets[@]}"; do
  build_dir="build/$preset"
  time_output="$(mktemp)"
  echo "==> Checking $preset"
  if /usr/bin/time -v -o "$time_output" bash -c '
    set -e
    cmake --preset "$1"
    cmake --build --preset "$1" --parallel 1
    ctest --test-dir "$2" --output-on-failure --parallel 1
  ' _ "$preset" "$build_dir"; then
    config_status=0
  else
    config_status=$?
    overall_status=1
  fi

  cat "$time_output"
  peak_rss="$(awk -F ": " '/Maximum resident set size/ { print $2 }' "$time_output")"
  if [[ -n "$peak_rss" ]]; then
    echo "Peak RSS ($preset): $peak_rss kB"
  else
    echo "Peak RSS ($preset): unavailable"
  fi
  rm -f -- "$time_output"
  rm -rf -- "$build_dir"

  if ((config_status != 0)); then
    echo "Configuration $preset failed with status $config_status" >&2
  fi
done

exit "$overall_status"
