#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <string>
#include <utility>
#include "job_status.hpp"
#include "job.hpp"
#include "job_run.hpp"
#include <memory>

struct ReadyJob {
    Job* job;
    int category;
    int user_priority;
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
    using FinishedCallback = std::function<std::vector<ReadyJob>(
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

            run->transition_to(Job_Status::RUNNING); // QUEUED already happened at push time
            Job_Status result = job->execute();

            std::size_t pushed_jobs = 0;
            {
                std::lock_guard<std::mutex> completion_lock(queue_mutex_);
                run->transition_to(result);
                const auto ready_jobs = on_finished_(job->id(), result);
                for (const auto& ready_job : ready_jobs) {
                    push_job_locked(
                        ready_job.job, ready_job.category, ready_job.user_priority);
                    ++pushed_jobs;
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