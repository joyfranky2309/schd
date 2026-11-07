#pragma once

#include <cstddef>
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

    std::unordered_map<std::string, JobEntry> registry_;
    DAG dag_;
    ThreadPool pool_;

public:
    explicit Scheduler(size_t num_threads)
        : pool_(num_threads, [this](const std::string& id, Job_Status result) {
            std::vector<ReadyJob> ready_jobs;
            if (result != Job_Status::SUCCESS) {
                // Failure-skipping policy is not implemented yet.
                return ready_jobs;
            }

            for (const auto& ready_id : dag_.mark_finished(id)) {
                const auto& entry = registry_.at(ready_id);
                ready_jobs.push_back(
                    ReadyJob{entry.job, entry.category, entry.user_priority});
            }
            return ready_jobs;
        })
    {
    }

    // Job pointers are non-owning; callers must keep jobs alive longer than this Scheduler.
    void add_job(Job* job, int category, int user_priority)
    {
        registry_.insert_or_assign(
            job->id(), JobEntry{job, category, user_priority});
        dag_.add_job(job->id());
    }

    void add_dependency(const std::string& from, const std::string& to)
    {
        if (registry_.find(from) == registry_.end() ||
            registry_.find(to) == registry_.end()) {
            throw std::invalid_argument("dependency references an unregistered job");
        }
        dag_.add_edge(from, to);
    }

    void start()
    {
        if (dag_.has_cycle()) {
            throw std::runtime_error("cannot start scheduler with a cyclic DAG");
        }

        for (const auto& id : dag_.initial_ready()) {
            const auto& entry = registry_.at(id);
            pool_.push_job(entry.job, entry.category, entry.user_priority);
        }
    }
};
