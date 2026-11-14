# schd

`schd` is a C++20 dependency-aware job scheduler that keeps dependency tracking,
worker execution, and orchestration in distinct layers. The DAG decides which
jobs become ready, the thread pool orders work by category, user priority, and
insertion order, and the scheduler coordinates the job registry and the worker
pool.

## Current milestone

This repository is currently on milestone M0: build hygiene and test harness.
The project is split into a reusable core library (`schd_core`), a small
placeholder CLI (`schd`), and a GoogleTest suite (`schd_tests`). The old
ad-hoc checks have been preserved as GTest cases and are run via CTest.

## Build and test

Release build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

ThreadSanitizer build:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Release -DSCHD_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure --repeat until-fail:20
```

AddressSanitizer + UBSan build:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Release -DSCHD_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

`SCHD_ASAN` and `SCHD_TSAN` are mutually exclusive.

## Placeholder CLI

The current CLI is intentionally minimal while the scheduler core is being
stabilized; it prints usage and will be replaced by the real job-file runner in
M4.

```sh
./build/schd
```

## Current limitations

- Job pointers are non-owning; callers must keep them alive for the scheduler's
  lifetime.
- The current setup is still the M0 scaffolding; the CLI, loader, job-file
  validation, and richer result handling are not in place yet.
- Failed jobs still cause dependent jobs to be skipped, but the run history and
  retry machinery have not been implemented yet.
