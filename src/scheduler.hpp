#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dag.hpp"
#include "threadpool.hpp"

class Scheduler {
    struct JobEntry {
        Job* job;
        int category;
        int user_priority;
    };

    std::unordered_map<std::string, JobEntry> registry_;
    DAG dag_;
    std::unordered_set<std::string> skipped_jobs_;
    bool started_{false};
    ThreadPool pool_;

public:
    explicit Scheduler(size_t num_threads)
        : pool_(num_threads, [this](const std::string& id, Job_Status result) {
            FinishedJobs finished_jobs;
            if (result == Job_Status::SUCCESS) {
                for (const auto& ready_id : dag_.mark_finished(id)) {
                    if (skipped_jobs_.find(ready_id) != skipped_jobs_.end()) {
                        continue;
                    }
                    const auto& entry = registry_.at(ready_id);
                    finished_jobs.ready.push_back(
                        ReadyJob{entry.job, entry.category, entry.user_priority});
                }
            } else {
                for (const auto& dependent_id : dag_.dependents_of(id)) {
                    if (!skipped_jobs_.insert(dependent_id).second) {
                        continue;
                    }
                    const auto& entry = registry_.at(dependent_id);
                    finished_jobs.skipped.push_back(
                        ReadyJob{entry.job, entry.category, entry.user_priority});
                }
            }
            return finished_jobs;
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
        started_ = true;

        std::vector<ReadyJob> initially_ready;
        for (const auto& id : dag_.initial_ready()) {
            const auto& entry = registry_.at(id);
            initially_ready.push_back(
                ReadyJob{entry.job, entry.category, entry.user_priority});
        }
        pool_.push_jobs(initially_ready);
    }
};
