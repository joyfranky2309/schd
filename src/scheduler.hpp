#pragma once

#include <cstddef>
#include <chrono>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "dag.hpp"
#include "job_run.hpp"
#include "threadpool.hpp"

struct RunRecord {
    std::string job_id;
    int attempt;
    Job_Status status;
    std::optional<std::chrono::system_clock::time_point> queued_at;
    std::optional<std::chrono::system_clock::time_point> started_at;
    std::optional<std::chrono::system_clock::time_point> finished_at;
    int exit_code;
    std::string message;
};

class Scheduler {
    struct JobEntry {
        Job* job;
        int category;
        int user_priority;
    };

    FinishedJobs handle_finished(
        const std::string& id,
        const ExecResult& result,
        const JobRun& run)
    {
        FinishedJobs finished_jobs;
        if (final_statuses_.find(id) != final_statuses_.end()) {
            return finished_jobs;
        }
        auto& records = history_[id];
        const int attempt = result.status == Job_Status::SKIPPED
            ? 0
            : static_cast<int>(records.size()) + 1;
        records.push_back(RunRecord{
            id,
            attempt,
            result.status,
            run.queued_time(),
            run.start_time(),
            run.end_time(),
            result.exit_code,
            result.message});

        if (result.status == Job_Status::FAILED) {
            const auto& entry = registry_.at(id);
            if (attempt < entry.job->max_attempts()) {
                finished_jobs.ready.push_back(
                    ReadyJob{entry.job, entry.category, entry.user_priority});
                return finished_jobs;
            }
        } else if (result.status == Job_Status::SKIPPED) {
            final_statuses_.try_emplace(id, result.status);
            completion_latch_->count_down();
            return finished_jobs;
        }

        final_statuses_.try_emplace(id, result.status);

        if (result.status == Job_Status::SUCCESS) {
            for (const auto& ready_id : dag_.mark_finished(id)) {
                const auto& entry = registry_.at(ready_id);
                finished_jobs.ready.push_back(
                    ReadyJob{entry.job, entry.category, entry.user_priority});
            }
        } else {
            for (const auto& dependent_id : dag_.transitive_dependents(id)) {
                if (!dag_.mark_skipped(dependent_id)) {
                    continue;
                }
                const auto& entry = registry_.at(dependent_id);
                finished_jobs.skipped.push_back(
                    FinishedJobs::SkippedJob{
                        ReadyJob{entry.job, entry.category, entry.user_priority}, id});
            }
        }

        completion_latch_->count_down();
        return finished_jobs;
    }

    FinishedJobs handle_callback_error(
        const std::string& id,
        const ExecResult& result,
        const JobRun& run)
    {
        FinishedJobs finished_jobs;
        if (final_statuses_.find(id) != final_statuses_.end()) {
            return finished_jobs;
        }

        auto& records = history_[id];
        const int attempt = static_cast<int>(records.size()) + 1;
        records.push_back(RunRecord{
            id,
            attempt,
            Job_Status::FAILED,
            run.queued_time(),
            run.start_time(),
            run.end_time(),
            result.exit_code,
            result.message});
        final_statuses_.try_emplace(id, Job_Status::FAILED);

        for (const auto& dependent_id : dag_.transitive_dependents(id)) {
            if (!dag_.mark_skipped(dependent_id)) {
                continue;
            }
            const auto& entry = registry_.at(dependent_id);
            finished_jobs.skipped.push_back(
                FinishedJobs::SkippedJob{
                    ReadyJob{entry.job, entry.category, entry.user_priority}, id});
        }
        completion_latch_->count_down();
        return finished_jobs;
    }

    std::unordered_map<std::string, JobEntry> registry_;
    DAG dag_;
    std::unordered_map<std::string, Job_Status> final_statuses_;
    std::unordered_map<std::string, std::vector<RunRecord>> history_;
    std::unique_ptr<std::latch> completion_latch_;
    bool started_{false};
    bool waited_{false};
    ThreadPool pool_;

public:
    explicit Scheduler(size_t num_threads)
        : pool_(num_threads, [this](
              const std::string& id,
              const ExecResult& result,
              const JobRun& run) {
              return handle_finished(id, result, run);
          }, [this](
              const std::string& id,
              const ExecResult& result,
              const JobRun& run) {
              return handle_callback_error(id, result, run);
          })
    {
    }

    // Job pointers are non-owning; callers must keep jobs alive longer than this Scheduler.
    void add_job(Job* job, int category, int user_priority)
    {
        if (started_) {
            throw std::logic_error("cannot add jobs after the scheduler has started");
        }
        if (job->max_attempts() < 1) {
            throw std::invalid_argument("max_attempts must be at least 1");
        }
        registry_.insert_or_assign(
            job->id(), JobEntry{job, category, user_priority});
        dag_.add_job(job->id());
    }

    void add_dependency(const std::string& from, const std::string& to)
    {
        if (started_) {
            throw std::logic_error("cannot add dependencies after the scheduler has started");
        }
        if (registry_.find(from) == registry_.end() ||
            registry_.find(to) == registry_.end()) {
            throw std::invalid_argument("dependency references an unregistered job");
        }
        dag_.add_edge(from, to);
    }

    void start()
    {
        if (started_) {
            throw std::logic_error("scheduler has already started");
        }
        if (dag_.has_cycle()) {
            throw std::runtime_error("cannot start scheduler with a cyclic DAG");
        }

        std::vector<ReadyJob> initially_ready;
        for (const auto& id : dag_.initial_ready()) {
            const auto& entry = registry_.at(id);
            initially_ready.push_back(
                ReadyJob{entry.job, entry.category, entry.user_priority});
        }
        completion_latch_ = std::make_unique<std::latch>(
            static_cast<std::ptrdiff_t>(registry_.size()));
        started_ = true;
        pool_.push_jobs(initially_ready);
    }

    void wait()
    {
        if (!started_) {
            throw std::logic_error("scheduler has not started");
        }
        // The latch synchronizes callback state updates before final_status is read.
        completion_latch_->wait();
        waited_ = true;
    }

    Job_Status final_status(const std::string& id) const
    {
        if (!waited_) {
            throw std::logic_error("final status is available only after wait");
        }
        return final_statuses_.at(id);
    }

    std::vector<RunRecord> history(const std::string& id) const
    {
        if (!waited_) {
            throw std::logic_error("run history is available only after wait");
        }
        return history_.at(id);
    }
};
