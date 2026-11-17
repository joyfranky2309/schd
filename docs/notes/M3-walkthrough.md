## Files changed
- `src/job_run.hpp` — added queued-time access.
- `src/job_run.cpp` — captures queued, started, and finished timestamps.
- `src/threadpool.hpp` — passes completed runs to the callback and carries failed-ancestor information for skipped jobs.
- `src/scheduler.hpp` — records attempt snapshots, retries, and exposes history after wait.
- `tests/test_job_run.cpp` — verifies queued timestamps are captured.
- `tests/test_scheduler.cpp` — covers retry counts, ordering, attempt numbering, timestamps, and skipped history.
- `README.md` — documents immediate retry and the no-backoff limitation.
- `docs/notes/M3-walkthrough.md` — records the M3 implementation.

## Functions added or changed
- `JobRun::queued_time()` exposes the timestamp captured on transition to QUEUED.
- `JobRun::transition_to()` captures queued, started, and terminal timestamps.
- `ThreadPool::worker_loop()` supplies each completed `JobRun` to the callback and queues returned retries with fresh runs.
- `Scheduler::handle_finished()` appends snapshots, retries while attempts remain, and applies permanent-failure skipping.
- `Scheduler::add_job()` rejects jobs whose `max_attempts` is below one.
- `Scheduler::history()` returns a copy after `wait()`.

## Invariants
- I1 — `Scheduler::handle_finished()` appends history while invoked by the callback under `ThreadPool::queue_mutex_`; `wait()` synchronizes history reads.
- I2 — `Scheduler::handle_finished()` returns retry/ready jobs; `ThreadPool::worker_loop()` pushes them internally without calling a public pool method.
- I3 — `ThreadPool::worker_loop()` releases the queue lock before calling `Job::execute()`.
- I4 — `ThreadPool::worker_loop()`, `push_jobs()`, and the destructor notify only after releasing the queue lock.
- I5 — `ThreadPool::worker_loop()` catches execution and completion-callback exceptions so they do not escape worker threads.
- I6 — `Scheduler` declares registry, DAG, final statuses, history, and latch before `pool_`.
- I7 — `Scheduler::add_job()` retains non-owning job pointers; callers must keep jobs alive for the scheduler lifetime.

## Locking map
- `ThreadPool::worker_loop()` acquires `queue_mutex_` with `unique_lock` to wait and pop work; it releases the lock while executing the job.
- `ThreadPool::worker_loop()` acquires `queue_mutex_` with `lock_guard` around result recording, callback work, retries, and skipped-job processing.
- `ThreadPool::push_jobs()` acquires `queue_mutex_` while creating queued runs and inserting work.
- `ThreadPool::~ThreadPool()` acquires `queue_mutex_` while setting `stopping`.
- Condition-variable notifications in these paths occur after their lock scopes.

## Assumptions
- `max_attempts` counts total executions; retries are immediate and receive a new queue sequence.
- A skipped job names the permanently failed ancestor that caused its skip, including for transitive dependents.
- Run records are value snapshots that are not modified after appending; skipped records use attempt 0.

## Three review questions
- Why does a retry return from the callback without calling `DAG::mark_finished()`?
- Why must the callback return work instead of calling a public `ThreadPool` method?
- How does a skipped job's history retain the identity of the failed ancestor?
