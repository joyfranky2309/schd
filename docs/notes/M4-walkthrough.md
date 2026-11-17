## Files changed
- `CMakeLists.txt` — fetches nlohmann_json and adds the M4 sources and CLI test dependency.
- `README.md` — documents the JSON job file, CLI commands, and current limitations.
- `src/main.cpp` — implements `run` and `validate` commands with hand-written argument parsing.
- `src/job_loader.hpp` — declares the loaded job-file and registration data types.
- `src/job_loader.cpp` — parses JSON and validates job definitions, dependencies, and worker count.
- `src/shell_job.hpp` — declares the shell-backed job type.
- `src/shell_job.cpp` — spawns `/bin/sh -c`, waits, and maps exit and signal results.
- `src/threadpool.hpp` — safely stops and joins any workers if thread creation fails.
- `tests/test_job_loader.cpp` — tests loader defaults, valid registration, and job-specific errors.
- `tests/test_shell_job.cpp` — tests successful, nonzero, and signal-terminated commands.
- `tests/test_cli.cpp` — runs the built CLI on a failing branch and verifies the summary.
- `docs/notes/M4-walkthrough.md` — records the M4 implementation.

## Functions added or changed
- `ShellJob::execute()` uses `posix_spawn` and `waitpid`, mapping exit status and signals to `ExecResult`.
- `load_job_file()` parses the file, creates owned shell jobs, and returns their registration data.
- `parse_worker_count()` validates the optional CLI worker override.
- `validate_acyclic()` constructs a temporary DAG and rejects cycles without running jobs.
- `validate_command()` loads and checks a file, then prints `OK`.
- `run_command()` registers jobs and dependencies, waits for completion, prints the summary, and selects the run exit code.
- `execute_cli()` handles the supported commands and argument shapes.
- `ThreadPool::ThreadPool()` stops and joins already-created workers if a later thread creation fails.

## Invariants
- I1 — The loader and graph validation run before `Scheduler::start()`; scheduler completion and history updates remain in the pool callback under `queue_mutex_`.
- I2 — Completion callbacks return jobs to the pool; they do not call its public methods.
- I3 — The worker releases `queue_mutex_` before calling `ShellJob::execute()`.
- I4 — Queue notifications, including the constructor-failure `notify_all`, occur after releasing the queue lock.
- I5 — `ShellJob::execute()` returns failures for spawn/wait errors; worker execution and callback exceptions remain contained by `ThreadPool::worker_loop()`.
- I6 — Callback-touched `Scheduler` members remain declared before `pool_`.
- I7 — `LoadedJobFile` owns its `unique_ptr<Job>` vector and is declared before the `Scheduler` in `run_command()`.

## Locking map
- `ShellJob`, the JSON loader, and CLI parsing/validation add no locks.
- `ThreadPool::worker_loop()` uses `unique_lock` while waiting and removing queued work, releases it during execution, and uses `lock_guard` around callback state changes and returned-job insertion.
- `ThreadPool::push_jobs()` uses `lock_guard` while creating queued runs and inserting work.
- `ThreadPool` construction failure uses `lock_guard` to set `stopping`, releases it before notifying, then joins created workers.
- `ThreadPool::~ThreadPool()` uses `lock_guard` to set `stopping` and notifies after releasing the lock.

## Assumptions
- Job `id`, `type`, `command`, and `category` are required; only priority, max attempts, dependencies, and workers have the documented defaults.
- Worker counts must be positive integers; the command-line value overrides the file value.
- Duration is the sum of execution time (`started_at` to `finished_at`) across attempts; skipped jobs have zero duration.
- If process creation or waiting fails before an exit status exists, `ShellJob` returns FAILED with exit code -1 and an explanatory message.

## Three review questions
- Why does `run_command()` keep `LoadedJobFile` alive before constructing the `Scheduler`?
- How does `ShellJob::execute()` distinguish a nonzero exit from signal termination?
- Why does `validate_acyclic()` build a DAG instead of starting a `Scheduler`?
