## 1. Files changed
- `CMakeLists.txt` — keeps the QuantLib build opt-in and accepts both config-package and system header/library installations.
- `README.md` — documents `cpu_pricing`, `--verbose`, the optional QuantLib build, and a `quantlib_bond` job file.
- `docs/BLOCKERS.md` — records the WSL memory issue accurately and the verified system-package installation.
- `src/cpu_pricing_job.cpp` — reports one deterministic present-value price.
- `src/job_loader.cpp` — validates shared pricing parameters and conditionally loads `quantlib_bond`.
- `src/main.cpp` — adds opt-in final-history messages after the existing run table.
- `src/quantlib_bond_job.hpp/.cpp` — adds the optional QuantLib fixed-rate bond implementation.
- `tests/test_cli.cpp` — checks verbose pricing output.
- `tests/test_cpu_pricing_job.cpp` — checks par pricing, yield monotonicity, and deterministic messages.
- `tests/test_job_loader.cpp` — checks pricing parameter/date validation and feature gating.
- `tests/test_quantlib_bond_job.cpp` — checks QuantLib pricing, date restoration, and concurrent scheduling.

## 2. Functions added or changed
- `CpuPricingJob::execute()` repeats deterministic present-value calculations and reports `price`, `samples`, and `settlement_date`.
- `is_valid_settlement_date()` checks strict `YYYY-MM-DD` syntax and calendar validity.
- `load_job_file()` validates the pricing ranges and constructs `QuantLibBondJob` only in enabled builds.
- `QuantLibBondJob::execute()` sets an evaluation date, prices an annual-coupon `FixedRateBond` against a flat curve, reports NPV/clean/dirty/accrued values, and restores the previous date.
- `run_command()` prints each job's final history message only when `--verbose` is set.

## 3. Invariants
- I1 — Loading and graph validation remain before scheduling; scheduler history updates remain in the existing completion callback.
- I2 — Completion callbacks continue returning ready jobs to the pool without calling its public methods.
- I3 — Job execution remains outside `queue_mutex_`; the QuantLib mutex does not change this.
- I4 — Queue notifications and lock ordering are unchanged.
- I5 — QuantLib errors become FAILED results with their error text; other job exceptions retain the existing worker handling.
- I6 — Callback-touched `Scheduler` members and the pool declaration order are unchanged.
- I7 — The loaded job file continues owning job objects for the lifetime of the scheduler.

## 4. Locking map
- A static mutex in `quantlib_bond_job.cpp` serializes the whole QuantLib pricing section because `Settings::evaluationDate()` is process-global.
- An RAII guard restores the prior evaluation date before releasing that mutex.
- The new mutex is independent of the thread pool's `queue_mutex_`; no scheduler or DAG locking changed.

## 5. Assumptions
- `cpu_pricing` is a synthetic CPU-bound workload, not Monte Carlo pricing or a QuantLib pricer.
- Pricing parameters use `face_value > 0`, rates in `[0, 1]`, maturity years in `[1, 100]`, and a real optional settlement date; the default is `2024-01-02`.
- QuantLib is optional and can be found as a CMake config package or through system headers and library.
- Validation completed in Release, ASan/UBSan, and TSan with QuantLib both disabled and enabled; the different-date 4-worker QuantLib test also passed 20 repeated TSan runs.

## 6. Three review questions
- Why does `QuantLibBondJob` serialize pricing even though the scheduler already runs jobs on multiple workers?
- How does verbose output select the final message after retries without changing the default table?
- Which QuantLib-related code is absent from the default build?
