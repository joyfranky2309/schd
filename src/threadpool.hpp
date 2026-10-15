#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <queue>
#include "job_status.hpp"
#include "job.hpp"
#include "job_run.hpp"
#include <memory>

struct QueueItem {
    Job* job;
    std::unique_ptr<JobRun> run;
    int category;
    int user_priority;
    uint64_t sequence;
};

struct QueueItemCompare {
    bool operator()(const QueueItem& a, const QueueItem& b) const {
        // return true if `a` should be considered LOWER priority than `b`
        // (i.e., `b` should come out of the queue first)
        if(a.category!=b.category) return a.category < b.category;
        if(a.user_priority!=b.user_priority) return a.user_priority < b.user_priority;
        return a.sequence > b.sequence;
    }
};
class ThreadPool
{
    std::vector<std::thread> threads;
    bool stopping=false;
    std::mutex queue_mutex_;
    std::condition_variable cv;
    std::priority_queue<QueueItem,std::vector<QueueItem>,QueueItemCompare> ready_queue;
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
            run->transition_to(result);
        }
    }

public:
 std::atomic<uint64_t> next_sequence_{0};
    void push_job(Job *job,int category,int user_priority)
    {
        auto run = std::make_unique<JobRun>(job->id());
        run->transition_to(Job_Status::QUEUED);
        uint64_t seq=next_sequence_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            ready_queue.push(QueueItem{job, std::move(run),category,user_priority,seq});
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