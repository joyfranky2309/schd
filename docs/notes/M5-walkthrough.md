## 1. Files changed
- `CMakeLists.txt` — keeps QuantLib opt-in and accepts config-package or system header/library installations.
- `.github/workflows/ci.yml` — installs the Ubuntu QuantLib package and builds/tests the optional integration.
- `README.md` — documents `cpu_pricing`, `--verbose`, optional QuantLib builds, and issue-date behavior.
- `docs/BLOCKERS.md` — records the resolved system-package blocker.
- `src/cpu_pricing_job.cpp` — reports one deterministic present-value price.
- `src/job_loader.cpp` — validates shared pricing parameters/dates, derives issue date, and conditionally loads QuantLib jobs.
- `src/main.cpp` — adds opt-in final-history messages after the existing run table.
- `src/quantlib_bond_job.hpp/.cpp` — prices from issue date to maturity and reports the evaluation date read back from QuantLib.
- `tests/test_cli.cpp` — covers verbose CPU and QuantLib output.
- `tests/test_cpu_pricing_job.cpp` — checks par pricing, yield monotonicity, and deterministic messages.
- `tests/test_job_loader.cpp` — covers pricing validation, issue-date behavior, and feature gating.
- `tests/test_quantlib_bond_job.cpp` — checks pricing, accrued interest, date restoration, and concurrent evaluation-date results.

## 2. Functions added or changed
- `CpuPricingJob::execute()` repeats deterministic present-value calculations and reports `price`, `samples`, and `settlement_date`.
- `parse_calendar_date()` validates real dates in strict `YYYY-MM-DD` form for settlement and issue dates.
- `default_issue_date()` derives the date three calendar months before settlement, clamping to the prior month's last day when needed.
- `load_job_file()` requires an explicit issue date to precede settlement and supplies the derived default when it is omitted.
- `QuantLibBondJob::execute()` builds the annual-coupon schedule from issue date through issue-date-plus-maturity, prices it, and includes the evaluation date read back under the QuantLib lock.
- `run_command()` prints each job's final history message only when `--verbose` is set.

## 3. Invariants
- I1 — Loading and graph validation remain before scheduling; scheduler history updates remain in the existing completion callback.
- I2 — Completion callbacks continue returning ready jobs to the pool without calling its public methods.
- I3 — Job execution remains outside `queue_mutex_`; the QuantLib mutex does not change this.
- I4 — Queue notifications and lock ordering are unchanged.
- I5 — QuantLib and standard exceptions become FAILED results with their error text; other job exceptions retain existing worker handling.
- I6 — Callback-touched `Scheduler` members and pool declaration order are unchanged.
- I7 — The loaded job file continues owning job objects for the lifetime of the scheduler.

## 4. Locking map
- A static mutex in `quantlib_bond_job.cpp` serializes each pricing section because QuantLib's evaluation date is process-global.
- An RAII guard restores the previous evaluation date before releasing the mutex; the message records the date read back while protected.
- The mutex is independent of the thread pool's `queue_mutex_`; no scheduler or DAG locking changed.

## 5. Assumptions
- `issue_date` is optional, strictly precedes settlement, and defaults to three calendar months earlier; month-end defaults clamp to the last valid calendar day.
- `cpu_pricing` remains a synthetic CPU-bound workload, not Monte Carlo pricing or a QuantLib pricer.
- Pricing parameters use `face_value > 0`, rates in `[0, 1]`, maturity years in `[1, 100]`, and a real optional settlement date; the default is `2024-01-02`.
- QuantLib remains opt-in; it can be found as a config package or through system headers and library. CI installs `libquantlib0-dev` and tests an enabled Release build.
- The current Release/ASan/UBSan suite is 46 tests with `SCHD_WITH_QUANTLIB=OFF`; QuantLib-enabled builds add the option-specific pricing tests, for a 53-test suite in the current test set.
- The installed QuantLib 1.41 configuration does not define `QL_ENABLE_SESSIONS`.

## 6. Three review questions
- Why must QuantLib pricing be serialized by a mutex even when jobs execute on separate workers?
- How does the loader ensure a default or explicit issue date is a real date strictly before settlement?
- How does verbose output select the final message after retries without changing the default table?
