#include<thread>
#include<mutex>
#include<condition_variable>
#include<vector>
#include<queue>
#include "job.hpp"
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

        }
    }
    public:
    //
};