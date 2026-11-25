# schd

[![CI](https://github.com/joyfranky2309/schd/actions/workflows/ci.yml/badge.svg)](https://github.com/joyfranky2309/schd/actions/workflows/ci.yml)

`schd` is a C++20 DAG-based scheduler that runs shell and optional pricing jobs through a priority-aware thread pool, skips only failed dependency branches, retries failed jobs immediately, and keeps an in-memory audit trail of every attempt.

```mermaid
flowchart LR
    CLI[CLI] --> Scheduler
    Scheduler --> Registry[registry]
    Scheduler --> DAG[DAG]
    Scheduler --> ThreadPool[ThreadPool]
    Registry --> Workers[workers]
    DAG --> ThreadPool
    ThreadPool --> Workers
    Workers --> JobRun[JobRun]
    JobRun --> History[RunRecord history]
```

## Locked design decisions

- **Job and JobRun are separate:** `Job` is the reusable definition, while each attempt gets a fresh `JobRun` snapshot.
- **Inheritance is intentionally narrow:** subclasses implement only `execute()`; dependency and status rules remain shared.
- **The DAG owns edges:** jobs have no dependency state, and `DAG` owns readiness and skipped-node state.
- **Priority has three tiers:** category, user priority, then insertion sequence provide deterministic FIFO ordering within a tier; EOD outranks ADHOC.
- **One scheduler lock protects schedule state:** indegree updates and ready-queue pushes occur in `ThreadPool::queue_mutex_`.
- **Runs are queued, not popped, with timestamps:** `JobRun` enters `QUEUED` at push time, making queue wait measurable.
- **Attempt numbers are derived:** an attempt is the number of existing run records for that job plus one.

## AutoSys comparison

- `schd` skips only transitive dependents of a permanent failure, while whole-box failure behavior is outside its scope (**owner to verify**).
- `schd` applies category and numeric priority to ad-hoc jobs; the comparable AutoSys ad-hoc priority behavior should be treated as the owner's verification item (**owner to verify**).
- `schd` stores per-job, per-attempt `RunRecord` audit history in memory; retainable/exportable AutoSys audit history is a separate implementation claim (**owner to verify**).

## Build

Release:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

AddressSanitizer plus UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Release -DSCHD_ASAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

ThreadSanitizer:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Release -DSCHD_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure --repeat until-fail:20
```

`SCHD_ASAN` and `SCHD_TSAN` are mutually exclusive.

QuantLib is optional:

```sh
sudo apt-get install -y libquantlib0-dev
cmake -S . -B build-quantlib \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCHD_WITH_QUANTLIB=ON
cmake --build build-quantlib --parallel
ctest --test-dir build-quantlib --output-on-failure
```

For vcpkg, add `-DCMAKE_TOOLCHAIN_FILE=$HOME/projects/vcpkg/scripts/buildsystems/vcpkg.cmake`.

## Run

```sh
./build/schd run jobs.json
./build/schd run jobs.json --workers 4
./build/schd validate jobs.json
```

Shell example:

```json
{
  "workers": 7,
  "jobs": [
    {
      "id": "extract",
      "type": "shell",
      "command": "echo extract",
      "category": "EOD",
      "priority": 5,
      "max_attempts": 3
    },
    {
      "id": "load",
      "type": "shell",
      "command": "echo load",
      "category": "EOD",
      "depends_on": ["extract"]
    }
  ]
}
```

Optional QuantLib pricing example:

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
        "settlement_date": "2024-01-02"
      }
    }
  ]
}
```

## Benchmark

The benchmark uses 1,000 in-process jobs in 20 layers, with 50 jobs per layer and 1–3 dependencies from the previous layer. It uses a fixed seed and five runs per configuration, reporting the median. No-op jobs and 1 ms sleep jobs are both benchmarked.

Measured on the repository machine with `hardware_concurrency: 12` on 2026-11-25.

| variant | workers | wall ms | jobs/sec | speedup vs 1 worker | scheduling overhead us/job |
|---|---:|---:|---:|---:|---:|
| no-op | 1 | 0.994 | 1006024.1 | 1.000 | 0.994 |
| no-op | 2 | 3.119 | 320660.1 | 0.319 | 3.119 |
| no-op | 4 | 3.495 | 286138.3 | 0.284 | 3.495 |
| no-op | 7 | 4.746 | 210700.8 | 0.209 | 4.746 |
| sleep-1ms | 1 | 1191.159 | 839.5 | 1.000 | - |
| sleep-1ms | 2 | 603.610 | 1656.7 | 1.973 | - |
| sleep-1ms | 4 | 301.706 | 3314.5 | 3.948 | - |
| sleep-1ms | 7 | 168.688 | 5928.1 | 7.061 | - |

Run it with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target schd_bench --parallel
./build/schd_bench
```

## Limitations

- History is in memory only; no persistence is implemented.
- Retries are immediate; there is no backoff delay.
- There are no cron or calendar triggers.
- There is no REST API.
- Execution is limited to a single process.
- Shell jobs have no timeout.
- QuantLib support is optional; without it, the core and CPU-pricing job remain available.
