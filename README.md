# schd

A small C++20 dependency-aware job scheduler. It separates dependency tracking
(`DAG`), worker execution and priority ordering (`ThreadPool`), and job
registration and orchestration (`Scheduler`).

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/schd
```

The executable runs the built-in checks for job status transitions, dependency
ordering, priority ordering, failure propagation, and cycle rejection.

## ThreadSanitizer

```sh
cmake -S . -B build-tsan -DSCHD_TSAN=ON
cmake --build build-tsan
./build-tsan/schd
```

`SCHD_TSAN` cannot be enabled together with `SCHD_SANITIZE`.

## Current limitations

Job pointers are non-owning; callers must keep jobs alive for the scheduler's
lifetime. Failed jobs cause their dependent jobs to be skipped. Job run history,
retry handling, and reporting of final run states are not implemented yet.
