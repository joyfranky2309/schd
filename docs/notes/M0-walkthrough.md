# M0 walkthrough

## 1. Files changed
- CMakeLists.txt
- src/main.cpp
- README.md
- .github/workflows/ci.yml
- tests/test_job_status.cpp
- tests/test_job_run.cpp
- tests/test_dag.cpp
- tests/test_scheduler.cpp

## 2. Functions added or changed
- `CMakeLists.txt`: added `schd_core` library, `schd` executable, `schd_tests` target, sanitizer options, and GoogleTest setup.
- `src/main.cpp`: replaced the inlined validation script with a usage placeholder for the future CLI.
- `tests/test_job_status.cpp`: added the status transition and formatting checks.
- `tests/test_job_run.cpp`: moved the run timestamp and illegal-transition assertions into GTest.
- `tests/test_dag.cpp`: moved the DAG readiness and cycle checks into GTest.
- `tests/test_scheduler.cpp`: preserved the dependency-order, priority-order, exception-handling, and invalid-graph checks.

## 3. Invariants
- I1: queue mutation remains guarded by `ThreadPool::queue_mutex_` in `ThreadPool::push_jobs` and `ThreadPool::worker_loop`.
- I3: `Job::execute()` still runs without a pool lock held; the test jobs continue to do work in user code only.
- I4: notification calls remain outside the critical section as before.
- I5: worker exceptions remain caught in `ThreadPool::worker_loop` and callback failures are guarded.
- I6: `Scheduler` member order remains consistent in `scheduler.hpp`; `pool_` is still last and owns thread shutdown.
- I7: jobs are still caller-owned and live as long as the scheduler uses them.

## 4. Locking map
- `ThreadPool::push_jobs`: lock `queue_mutex_` while enqueuing `ReadyJob` objects and while updating the priority queue.
- `ThreadPool::worker_loop`: lock `queue_mutex_` while taking the next queued item and while invoking the completion callback; the callback is intentionally kept inside the lock and must not call public pool methods.
- `Scheduler::start`: no additional lock was introduced; the implementation still pushes ready jobs through the pool once per schedule.

## 5. Assumptions
- The scheduler semantics are intentionally unchanged for this milestone; only the build/test harness and project layout are refactored.
- The placeholder `schd` CLI is intentionally minimal because the real command parser arrives in M4.

## 6. Three review questions
- Why is the build split into a core library plus test executable instead of keeping everything in a single target?
- Why are the sanitizer flags mutually exclusive, and how does this protect the TSan build from invalid configs?
- Which assertions from the legacy `main.cpp` checks were preserved in the GTest suite, and why does that make CI more reliable?
