#include<thread>
#include<mutex>
#include<condition_variable>
#include<vector>
#include<queue>
#include "job_status.hpp" 
#include "job.hpp"
#include "job_run.hpp"
class ThreadPool{
    std::vector<std::thread> threads;
    bool stopping;
    std::mutex queue_mutex_;
    std::condition_variable cv;
    std::queue<Job*>ready_queue;
    void worker_loop(){
        while(true)
        {
            std::unique_lock<std::mutex>lock(queue_mutex_);
            cv.wait(lock,[this](){return !ready_queue.empty()||stopping;});
            if(stopping && ready_queue.empty())
            return;
            Job* job = ready_queue.front();
            ready_queue.pop();
            lock.unlock();
            JobRun run(job->id());
            run.transition_to(Job_Status::QUEUED);
            run.transition_to(Job_Status::RUNNING);
            Job_Status result = job->execute();
            run.transition_to(result);
        }
    }
    public:
    //
};