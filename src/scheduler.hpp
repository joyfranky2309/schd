#pragma once

#include <cstddef>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "dag.hpp"
#include "threadpool.hpp"

class Scheduler {
    struct JobEntry {
        Job* job;
        int category;
        int user_priority;
    };

    FinishedJobs handle_finished(
        const std::string& id,
        const ExecResult& result)
    {
        FinishedJobs finished_jobs;
        const bool inserted = final_statuses_.try_emplace(id, result.status).second;
        if (!inserted) {
            return finished_jobs;
        }

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
                    ReadyJob{entry.job, entry.category, entry.user_priority});
            }
        }

        completion_latch_->count_down();
        return finished_jobs;
    }

    std::unordered_map<std::string, JobEntry> registry_;
    DAG dag_;
    std::unordered_map<std::string, Job_Status> final_statuses_;
    std::unique_ptr<std::latch> completion_latch_;
    bool started_{false};
    bool waited_{false};
    ThreadPool pool_;

public:
    explicit Scheduler(size_t num_threads)
        : pool_(num_threads, [this](const std::string& id, const ExecResult& result) {
            return handle_finished(id, result);
        })
    {
    }

    // Job pointers are non-owning; callers must keep jobs alive longer than this Scheduler.
    void add_job(Job* job, int category, int user_priority)
    {
        if (started_) {
            throw std::logic_error("cannot add jobs after the scheduler has started");
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
};
