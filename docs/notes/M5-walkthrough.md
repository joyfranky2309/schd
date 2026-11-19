## 1. Files changed
- `CMakeLists.txt` — adds `SCHD_WITH_QUANTLIB` and the QuantLib package probe.
- `src/cpu_pricing_job.hpp/.cpp` — adds the plain-C++ pricing-style fallback job.
- `src/job_loader.cpp` — accepts `cpu_pricing`, validates its params, and constructs it.
- `tests/test_cpu_pricing_job.cpp` — checks fallback execution and result-message fields.
- `README.md` — documents the fallback and QuantLib build option.
- `docs/BLOCKERS.md` — explains why QuantLib could not be integrated in the time-box.

## 2. Functions added or changed
- `CpuPricingJob::execute()` runs a deterministic Monte-Carlo present-value loop and reports clean and dirty price fields.
- `load_job_file()` now validates `cpu_pricing` params and creates the fallback job.
- CMake now exposes `SCHD_WITH_QUANTLIB` and calls `find_package(QuantLib CONFIG REQUIRED)` when enabled.

## 3. Invariants
- I1 — Loading and graph setup remain outside worker callbacks; job execution state remains protected by the existing pool callback.
- I3 — `CpuPricingJob::execute()` runs with no queue lock held.
- I5 — The fallback catches no exceptions itself, while `ThreadPool` contains any exception and records a FAILED result.
- I7 — `LoadedJobFile` owns the fallback job and is declared before `Scheduler` in `run_command()`.

## 4. Locking map
- No new locks are added.
- `CpuPricingJob::execute()` runs outside `ThreadPool::queue_mutex_`.
- Existing scheduler and thread-pool completion locking is unchanged.

## 5. Assumptions
- QuantLib could not be found by vcpkg in the available environment, so the plan's fallback was selected.
- The fixed settlement date is `2024-01-02`.
- The fallback clean and dirty prices are the same deterministic present-value estimate, because this is workload fallback rather than bond accounting.

## 6. Three review questions
- Why does M5 use `cpu_pricing` instead of claiming a QuantLib implementation?
- Why does `CpuPricingJob::execute()` run outside the queue lock?
- What params does the fallback require, and what fixed settlement date does it document?
