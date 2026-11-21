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

`QuantLibBondJob` is optional and is compiled only when `SCHD_WITH_QUANTLIB=ON`.
Install QuantLib with the system package or vcpkg, then configure, build, and
test it:

```sh
sudo apt install libquantlib0-dev
cmake -S . -B build-quantlib \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCHD_WITH_QUANTLIB=ON
cmake --build build-quantlib
ctest --test-dir build-quantlib --output-on-failure
```

For a vcpkg installation, add
`-DCMAKE_TOOLCHAIN_FILE=$HOME/projects/vcpkg/scripts/buildsystems/vcpkg.cmake`
to the configure command. CMake accepts both a QuantLib config package and
system installations that provide the QuantLib headers and library.

The `quantlib_bond` job prices a fixed-rate bond with annual coupons, a
Thirty360 day counter, a flat yield curve, and a discounting bond engine.
QuantLib's process-wide evaluation date is serialized across these jobs and
restored after each execution.

## CLI

Run a job file or validate its dependency graph without executing it:

```sh
./build/schd run jobs.json
./build/schd run jobs.json --workers 4
./build/schd run jobs.json --verbose
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

QuantLib bond job example:

```json
{
  "jobs": [
    {
      "id": "bond-price",
      "type": "quantlib_bond",
      "category": "EOD",
      "params": {
        "face_value": 100.0,
        "coupon_rate": 0.05,
        "maturity_years": 5,
        "yield": 0.05,
        "issue_date": "2023-10-02",
        "settlement_date": "2024-01-02"
      }
    }
  ]
}
```

`issue_date` is optional and must be a real `YYYY-MM-DD` date before
`settlement_date`; when omitted, it defaults to three calendar months before
settlement.

## Limitations

- Job pointers are non-owning; callers must keep them alive for the scheduler's
  lifetime.
- Retries happen immediately without a backoff delay.
- Shell jobs have no timeout, and standard output is inherited.
- Run history is in memory only; there is no persistence, REST API, or
  cron/calendar trigger.
- `cpu_pricing` is a synthetic workload, not a market-accurate bond pricer.
