#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <string>
#include <utility>
#include <iostream>
#include "job_status.hpp"
#include "job.hpp"
#include "job_run.hpp"
#include <memory>

struct ReadyJob {
    Job* job;
    int category;
    int user_priority;
};

struct FinishedJobs {
    std::vector<ReadyJob> ready;
    std::vector<ReadyJob> skipped;
};

struct QueueItem {
    Job* job;
    std::unique_ptr<JobRun> run;
    int category;
    int user_priority;
    uint64_t sequence;
};

struct QueueItemCompare {
    bool operator()(const QueueItem& a, const QueueItem& b) const {
        if(a.category!=b.category) return a.category < b.category;
        if(a.user_priority!=b.user_priority) return a.user_priority < b.user_priority;
        return a.sequence > b.sequence;
    }
};
class ThreadPool
{
public:
    using FinishedCallback = std::function<FinishedJobs(
        const std::string& id, Job_Status result)>;

private:
    std::vector<std::thread> threads;
    bool stopping=false;
    std::mutex queue_mutex_;
    std::condition_variable cv;
    std::priority_queue<QueueItem,std::vector<QueueItem>,QueueItemCompare> ready_queue;
    uint64_t next_sequence_{0};
    FinishedCallback on_finished_;

    void push_job_locked(Job *job,int category,int user_priority)
    {
        auto run = std::make_unique<JobRun>(job->id());
        run->transition_to(Job_Status::QUEUED);
        const uint64_t seq=next_sequence_++;
        ready_queue.push(QueueItem{job, std::move(run),category,user_priority,seq});
    }

    void worker_loop()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv.wait(lock, [this]()
                    { return !ready_queue.empty() || stopping; });
            if (stopping && ready_queue.empty())
                return;
            QueueItem& item= const_cast<QueueItem&> (ready_queue.top());
            auto job=item.job;
            auto run=std::move(item.run);
            ready_queue.pop();
            lock.unlock();

            Job_Status result = Job_Status::FAILED;
            bool should_execute = true;
            try {
                run->transition_to(Job_Status::RUNNING);
            } catch (const std::exception& error) {
                std::cerr << "Job " << job->id()
                          << " could not enter RUNNING: " << error.what() << '\n';
                should_execute = false;
            } catch (...) {
                std::cerr << "Job " << job->id()
                          << " could not enter RUNNING due to an unknown error\n";
                should_execute = false;
            }

            if (should_execute) {
                try {
                    result = job->execute();
                } catch (const std::exception& error) {
                    std::cerr << "Job " << job->id()
                              << " failed with an exception: " << error.what() << '\n';
                } catch (...) {
                    std::cerr << "Job " << job->id()
                              << " failed with an unknown exception\n";
                }
            }

            std::size_t pushed_jobs = 0;
            {
                std::lock_guard<std::mutex> completion_lock(queue_mutex_);
                try {
                    run->transition_to(result);
                } catch (const std::exception& error) {
                    std::cerr << "Could not record completion for job " << job->id()
                              << ": " << error.what() << '\n';
                    result = Job_Status::FAILED;
                    try {
                        run->transition_to(result);
                    } catch (const std::exception& fallback_error) {
                        std::cerr << "Could not mark job " << job->id()
                                  << " as FAILED: " << fallback_error.what() << '\n';
                    }
                } catch (...) {
                    std::cerr << "Could not record completion for job " << job->id()
                              << " due to an unknown error\n";
                    result = Job_Status::FAILED;
                }

                try {
                    std::deque<ReadyJob> skipped_jobs;
                    const auto finished_jobs = on_finished_(job->id(), result);
                    for (const auto& ready_job : finished_jobs.ready) {
                        push_job_locked(
                            ready_job.job, ready_job.category, ready_job.user_priority);
                        ++pushed_jobs;
                    }
                    for (const auto& skipped_job : finished_jobs.skipped) {
                        skipped_jobs.push_back(skipped_job);
                    }

                    while (!skipped_jobs.empty()) {
                        const auto skipped_job = skipped_jobs.front();
                        skipped_jobs.pop_front();

                        JobRun skipped_run(skipped_job.job->id());
                        skipped_run.transition_to(Job_Status::SKIPPED);
                        std::cerr << "Job " << skipped_job.job->id()
                                  << " skipped because a dependency did not succeed\n";
                        const auto newly_skipped = on_finished_(
                            skipped_job.job->id(), Job_Status::SKIPPED);
                        for (const auto& ready_job : newly_skipped.ready) {
                            push_job_locked(
                                ready_job.job,
                                ready_job.category,
                                ready_job.user_priority);
                            ++pushed_jobs;
                        }
                        for (const auto& next_skipped : newly_skipped.skipped) {
                            skipped_jobs.push_back(next_skipped);
                        }
                    }
                } catch (const std::exception& error) {
                    std::cerr << "Could not process completion for job " << job->id()
                              << ": " << error.what() << '\n';
                } catch (...) {
                    std::cerr << "Could not process completion for job " << job->id()
                              << " due to an unknown error\n";
                }
            }
            for (std::size_t i = 0; i < pushed_jobs; ++i) {
                cv.notify_one();
            }
        }
    }

public:
    ThreadPool(size_t num_threads, FinishedCallback on_finished)
        : on_finished_(std::move(on_finished))
    {
        for (size_t i = 0; i < num_threads; ++i)
        {
            threads.emplace_back(&ThreadPool::worker_loop, this);
        }
    }

    void push_job(Job *job,int category,int user_priority)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            push_job_locked(job,category,user_priority);
        }
        cv.notify_one();
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stopping = true;
        }
        cv.notify_all();

        for (auto &t : threads)
        {
            t.join();
        }
    }
};