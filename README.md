# schd

`schd` is a C++20 dependency-aware job scheduler that keeps dependency tracking,
worker execution, and orchestration in distinct layers. The DAG decides which
jobs become ready, the thread pool orders work by category, user priority, and
insertion order, and the scheduler coordinates the job registry and the worker
pool.

## Current milestone

The scheduler supports dependency-aware execution, permanent-failure skipping,
immediate retries, and per-attempt run history. The `schd` CLI loads JSON job
files and runs shell jobs, while the reusable core library (`schd_core`) and
GoogleTest suite (`schd_tests`) remain independently buildable.

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

The optional QuantLib build switch is present for M5:

```sh
cmake -S . -B build-quantlib \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCHD_WITH_QUANTLIB=ON \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/projects/vcpkg/scripts/buildsystems/vcpkg.cmake
```

QuantLib is not available in the current build environment. M5 therefore ships
a `cpu_pricing` job as a pricing-style CPU workload. It is deterministic
Monte-Carlo present-value loop written in plain C++; it is not a QuantLib bond
pricer and does not provide a market-accurate bond price.

## CLI

Run a job file or validate its dependency graph without executing it:

```sh
./build/schd run jobs.json
./build/schd run jobs.json --workers 4
./build/schd validate jobs.json
```

Example job file:

```json
{
  "workers": 7,
  "jobs": [
    { "id": "extract", "type": "shell", "command": "echo extract", "category": "EOD", "max_attempts": 3 },
    { "id": "load", "type": "shell", "command": "echo load", "category": "EOD", "depends_on": ["extract"] }
  ]
}
```

Pricing-style CPU job example:

```json
{
  "jobs": [
    {
      "id": "cpu-price",
      "type": "cpu_pricing",
      "category": "EOD",
      "params": {
        "face_value": 100.0,
        "coupon_rate": 0.05,
        "maturity_years": 5,
        "yield": 0.05,
        "settlement_date": "2024-01-02"
      }
    }
  ]
}
```

## Limitations

- Job pointers are non-owning; callers must keep them alive for the scheduler's
  lifetime.
- Retries happen immediately without a backoff delay.
- Shell jobs have no timeout, and standard output is inherited.
- Run history is in memory only; there is no persistence, REST API, or
  cron/calendar trigger.
- M5 uses a deterministic plain-C++ pricing workload fallback; QuantLib
  integration is not claimed until its package is available.
