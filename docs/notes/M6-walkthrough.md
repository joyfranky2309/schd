# M6 walkthrough

## 1. Files changed
- `bench/bench_dag.cpp` — adds the seeded in-process no-op and 1 ms sleep DAG benchmark.
- `CMakeLists.txt`, `.github/workflows/ci.yml` — add the plain Release benchmark target and execute it in CI.
- `src/threadpool.hpp`, `src/scheduler.hpp` — harden completion callback errors and defer `std::cerr` output until after unlock.
- `tests/test_scheduler.cpp` — checks callback-error failure recording, logging count, and timed completion.
- `README.md` — replaces the overview with implemented behavior, build options, examples, limitations, and benchmark results.
- `docs/notes/M0-walkthrough.md`, `docs/notes/M1-walkthrough.md`, `docs/notes/M5-walkthrough.md` — correct stale claims and test counts.
- `.clang-format`, `src/`, `tests/`, `bench/` — record the 4-space attached-brace style and format C++ sources.
- `docs/notes/M6-walkthrough.md` — records this milestone.

## 2. Functions added or changed
- `generate_dag()` creates one fixed-seed, 1,000-job DAG with 20 layers and unique 1–3 dependencies per non-root job.
- `run_once()` runs the same edge set with in-process `NoopJob` or `SleepJob` objects and times `start()` through `wait()`.
- `benchmark()` and `median()` report five-run median durations and derived throughput/speedup metrics.
- `ThreadPool::worker_loop()` catches completion callback failures, runs the fallback handler, and prints collected messages only after releasing `queue_mutex_`.
- `Scheduler::handle_callback_error()` records a failed attempt when the job has not already reached a final scheduler state, skips its descendants, and counts down the latch.
- `Scheduler::Scheduler()` supplies the callback-error handler while keeping the callback-touched state before `pool_`.

## 3. Invariants
- I1 — The completion callback and its error fallback update scheduler state only while `ThreadPool::worker_loop()` holds `queue_mutex_`.
- I2 — `handle_finished()` and `handle_callback_error()` return work; the worker inserts it through its locked internal helper rather than public pool methods.
- I3 — The worker releases `queue_mutex_` before `Job::execute()`.
- I4 — `worker_loop()`, `push_jobs()`, the constructor failure path, and the destructor notify only after their queue-lock scopes.
- I5 — Execution and callback exceptions are caught in `worker_loop()`; the scheduler fallback records terminal failure and prevents the ordinary callback-throw case from leaving `wait()` blocked.
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
- The local benchmark measurement used 12 reported hardware threads on 2026-11-25. Test counts are 46 with QuantLib OFF and 53 with it ON.

## 6. Five review questions
- Why does the benchmark use in-process jobs rather than shell jobs?
- How does the callback-error fallback keep a scheduler wait from hanging and represent the failed job?
- Why are collected `std::cerr` messages emitted only after the queue lock is released?
- Why must every benchmark configuration reuse the same generated DAG?
- Why is the benchmark target excluded from ASan/TSan Release configurations?
