#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include "job_status.hpp"
#include "job.hpp"
#include "job_run.hpp"
#include <memory>
class ThreadPool
{
    std::vector<std::thread> threads;
    bool stopping=false;
    std::mutex queue_mutex_;
    std::condition_variable cv;
    std::queue<std::pair<Job *, std::unique_ptr<JobRun>>> ready_queue;
    void worker_loop()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv.wait(lock, [this]()
                    { return !ready_queue.empty() || stopping; });
            if (stopping && ready_queue.empty())
                return;
            auto [job, run] = std::move(ready_queue.front());
            ready_queue.pop();
            lock.unlock();

            run->transition_to(Job_Status::RUNNING); // QUEUED already happened at push time
            Job_Status result = job->execute();
            run->transition_to(result);
        }
    }

public:
    void push_job(Job *job)
    {
        auto run = std::make_unique<JobRun>(job->id());
        run->transition_to(Job_Status::QUEUED);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            ready_queue.push({job, std::move(run)});
        }
        cv.notify_one();
    }
    explicit ThreadPool(size_t num_threads) : stopping(false)
    {
        for (size_t i = 0; i < num_threads; ++i)
        {
            threads.emplace_back(&ThreadPool::worker_loop, this);
        }
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