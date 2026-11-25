# M6 walkthrough

## 1. Files changed
- `bench/bench_dag.cpp` — adds the seeded in-process no-op and 1 ms sleep DAG benchmark.
- `CMakeLists.txt`, `.github/workflows/ci.yml` — add the plain Release benchmark target and execute it in CI.
- `src/threadpool.hpp`, `src/scheduler.hpp` — harden completion callback errors and defer `std::cerr` output until after unlock.
- `tests/test_scheduler.cpp` — checks callback-error failure recording, logging count, timed completion, and throwing-hook recovery.
- `CMakeLists.txt`, `CMakePresets.json`, `scripts/check-all.sh` — add single-job standard/low-memory build configurations and per-configuration RSS reporting.
- `README.md` — documents scheduler behavior, QuantLib dates/locking, benchmark results, and small-machine builds.
- `docs/notes/M0-walkthrough.md`, `docs/notes/M1-walkthrough.md`, `docs/notes/M5-walkthrough.md` — correct stale claims and test counts.
- `.clang-format`, `src/`, `tests/`, `bench/` — record the 4-space attached-brace style and format C++ sources.
- `docs/notes/M6-walkthrough.md` — records this milestone and its follow-up fixes.

## 2. Functions added or changed
- `generate_dag()` creates one fixed-seed, 1,000-job DAG with 20 layers and unique 1–3 dependencies per non-root job.
- `run_once()` runs the same edge set with in-process `NoopJob` or `SleepJob` objects and times `start()` through `wait()`.
- `benchmark()` and `median()` report five-run median durations and derived throughput/speedup metrics.
- `ThreadPool::worker_loop()` catches completion callback failures, runs the fallback handler through a shared recovery helper, catches fallback-handler exceptions, and prints collected messages only after releasing `queue_mutex_`.
- `Scheduler::handle_callback_error()` records a failed attempt when the job has not already reached a final scheduler state, skips its descendants, and counts down the latch.
- `Scheduler::Scheduler()` supplies the callback-error handler while keeping the callback-touched state before `pool_`.
- `Scheduler::handle_finished()` invokes an empty-by-default private test hook before updating scheduler state.

## 3. Invariants
- I1 — The completion callback and its error fallback update scheduler state only while `ThreadPool::worker_loop()` holds `queue_mutex_`.
- I2 — `handle_finished()` and `handle_callback_error()` return work; the worker inserts it through its locked internal helper rather than public pool methods.
- I3 — The worker releases `queue_mutex_` before `Job::execute()`.
- I4 — `worker_loop()`, `push_jobs()`, the constructor failure path, and the destructor notify only after their queue-lock scopes.
- I5 — Execution and callback exceptions are caught in `worker_loop()`; fallback-handler exceptions are logged and contained, while the scheduler fallback records terminal failure and prevents the ordinary callback-throw case from leaving `wait()` blocked.
- I6 — `registry_`, `dag_`, statuses, history, and the latch remain declared before `pool_`.
- I7 — The benchmark owns jobs before constructing `Scheduler`; the scheduler retains only non-owning job pointers.

## 4. Locking map
- `ThreadPool::worker_loop()` uses `unique_lock` while waiting and removing a queued item, then unlocks for execution.
- `ThreadPool::worker_loop()` uses `lock_guard` around run completion, callbacks, fallback state changes, and internal queue insertion; it stores log messages in memory and prints them after the guard is destroyed.
- `ThreadPool::push_jobs()` uses `lock_guard` for queued-run creation and queue insertion; notifications follow unlock.
- `ThreadPool` construction failure and destruction use `lock_guard` to change `stopping`; both notify after unlock.
- Scheduler handlers and benchmark jobs acquire no additional locks.

## 5. Assumptions
- Each job's 1–3 previous-layer dependencies are distinct; the seed and generated edge list are shared by all benchmark configurations.
- The reported median is the middle duration of five samples; speedup compares each worker count against that variant's one-worker median.
- The benchmark is omitted from sanitizer-configured Release builds so sanitizer instrumentation is linked only to the test and CLI targets; the plain Release CI job builds and runs it.
- The benchmark measurement used WSL2 with 12 logical processors on 2026-11-25. Test counts are 48 with QuantLib OFF and 55 with it ON.
- No-op benchmark jobs take microseconds, so the one scheduler lock per completion dominates as worker count grows; 1 ms sleep jobs scale close to linearly with workers.
- The `check-all.sh` peak RSS values include configuration, build, and test for each sequential preset; low-memory presets add `-g0` and GCC garbage-collector parameters.

## 6. Five review questions
- Why does the benchmark use in-process jobs rather than shell jobs?
- How does the callback-error fallback keep a scheduler wait from hanging and represent the failed job?
- Why are collected `std::cerr` messages emitted only after the queue lock is released?
- Why must every benchmark configuration reuse the same generated DAG?
- Why is the benchmark target excluded from ASan/TSan Release configurations?

## 7. Follow-up fixes (A-E)

- The Run examples again show `--verbose`. The QuantLib example includes `issue_date`; it is optional, strictly before settlement, and defaults to three months earlier. QuantLib evaluation-date mutation is process-global and pricing is serialized with a mutex.
- The AutoSys comparison is limited to the specified `schd` behavior, attempt history, and missing incumbent features; it makes no additional claims about AutoSys internals.
- Benchmark text explains that no-op jobs are slower with more workers because each completion takes the scheduler lock while the jobs take only microseconds. The 1 ms sleep jobs scale close to linearly with worker count.
- A `Scheduler`-level regression test injects a one-shot throwing `handle_finished()` hook and checks that `wait()` returns, the failed job is `FAILED`, and its dependent is `SKIPPED`. The test passed 20 consecutive TSan runs. A separate ThreadPool test covers a throwing callback-error handler.
- CMake presets cover Release, ASan/UBSan, TSan, and QuantLib, each with a low-memory variant. Ninja serializes the heavy compile units through a size-one pool. `scripts/check-all.sh` builds/tests each configuration sequentially with one job, removes that configuration's build directory, and reports `/usr/bin/time -v` peak RSS.

| configuration | peak RSS |
|---|---:|
| release | 500540 kB |
| asan | 651364 kB |
| tsan | 495256 kB |
| quantlib | 497268 kB |
| release-lowmem | 334396 kB |
| asan-lowmem | 447752 kB |
| tsan-lowmem | 339000 kB |
| quantlib-lowmem | 334872 kB |

All eight configurations passed their test suites with zero compiler warnings.
