# M7 walkthrough

## 1. Files changed
- `Dockerfile` and `.dockerignore` add a small-memory multi-stage Ubuntu container image and Docker build context exclusions.
- `examples/etl.json` and `examples/pricing.json` provide container-compatible ETL and CPU-pricing examples.
- `.github/workflows/ci.yml` adds Docker validation and an optional SQLite build/test job.
- `CMakeLists.txt` adds `SCHD_WITH_SQLITE` and the system SQLite3 dependency.
- `src/history_store.hpp`, `src/history_store.cpp`, and `src/main.cpp` add persistent run history and CLI commands.
- `tests/test_history_store.cpp` and `tests/test_cli.cpp` add guarded SQLite and CLI coverage.
- `README.md` documents Docker and SQLite usage plus the related limitations.

## 2. Docker packaging
- The builder stage uses `ubuntu:24.04`, installs build tools and optional QuantLib/SQLite dependencies, and builds with `CMAKE_BUILD_PARALLEL_LEVEL=1`.
- `WITH_QUANTLIB` and `WITH_SQLITE` are Docker build arguments defaulting to `OFF`.
- The builder compiles only the `schd` target. The runtime stage copies only `/src/build/schd`, installs only `libsqlite3-0` or `libquantlib0v5` when requested, keeps `/bin/sh`, creates a non-root `schd` user, and uses `ENTRYPOINT ["schd"]`.
- CI builds the image without pushing it, validates `etl.json`, and accepts exit code 1 for the intentional failing branch.

## 3. SQLite design
- `HistoryStore` is an RAII, move-only wrapper around `sqlite3*`.
- Create/open mode uses `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE`; `schd history` uses `open_readonly()` with `SQLITE_OPEN_READONLY` and creates no tables or files.
- Missing read-only database files report `no such database: <path>` and make the CLI exit 2.
- Successful opens set a 3000 ms SQLite busy timeout.
- SQL is fixed except for filtered/unfiltered prepared statements, and every variable is bound through SQLite parameters.
- The `run_records` table stores run and job identity, attempt, status, timestamps, exit code, and message.
- `save_run()` wraps all rows for a run in one `BEGIN`/`COMMIT` transaction. COMMIT failures execute `ROLLBACK` before the SQLite error is thrown.
- SQLite history is used only after `Scheduler::wait()` returns, on the CLI main thread. It adds no scheduler or thread-pool locks.

## 4. CLI behavior
- `schd run <file> --db <path>` opens the database before loading and running jobs.
- Database-open failures are configuration/input failures and return exit code 2.
- A transaction/save failure is reported on stderr and returns exit code 1.
- `schd history <db> [--job ID] [--limit N]` returns rows newest first, with an optional job filter and limit.
- When SQLite support is not compiled, `--db` and `history` return exit code 2 with a clear built-without-SQLite message.

## 5. Persistence and test guarantees
- Failed, successful, and skipped attempts retain their scheduler-recorded attempt numbers.
- Multiple runs receive distinct generated run IDs.
- History is filtering and newest-first behavior is covered under `SCHD_WITH_SQLITE`.
- The CLI and store tests cover create mode, read-only existing databases, missing read-only database files, the bad database path, and the build-without-SQLite exit contract.
- Persistence occurs only after the run finishes. A crash mid-run loses that run, and no recovery journal or replay mechanism is provided.

## 6. Review questions
- What guarantees that SQLite writes do not introduce a new scheduler/thread-pool lock or alter I1-I7?
- How does `save_run()` ensure that all run records are committed together or rejected together?
- What is the difference between a database-open failure and a history-write failure in CLI exit behavior?
