#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "scheduler.hpp"
#include "stub_job.hpp"

namespace {
class ThrowingJob : public Job {
public:
    explicit ThrowingJob(std::string id) : Job(std::move(id), 1) {}

    Job_Status execute() override {
        throw std::runtime_error("expected test exception");
    }
};
}

TEST(Scheduler, DependencyOrderIsRespectedAcrossConcurrentRuns) {
    for (int iteration = 0; iteration < 30; ++iteration) {
        std::mutex execution_order_mutex;
        std::vector<std::string> execution_order;
        auto record_execution = [&](const std::string& id) {
            thread_local std::mt19937 random_engine(std::random_device{}());
            std::uniform_int_distribution<int> delay(0, 500);
            std::this_thread::sleep_for(std::chrono::microseconds(delay(random_engine)));
            std::lock_guard<std::mutex> lock(execution_order_mutex);
            execution_order.push_back(id);
        };

        StubJob jobA("job-A", 3, record_execution);
        StubJob jobB("job-B", 3, record_execution);
        StubJob jobC("job-C", 3, record_execution);
        StubJob jobD("job-D", 3, record_execution);
        StubJob jobE("job-E", 3, record_execution);

        {
            Scheduler scheduler(4);
            scheduler.add_job(&jobA, 1, 1);
            scheduler.add_job(&jobB, 1, 1);
            scheduler.add_job(&jobC, 1, 1);
            scheduler.add_job(&jobD, 1, 1);
            scheduler.add_job(&jobE, 1, 1);
            scheduler.add_dependency("job-A", "job-C");
            scheduler.add_dependency("job-B", "job-C");
            scheduler.add_dependency("job-C", "job-D");
            scheduler.start();
        }

        const auto position = [&](const std::string& id) {
            return std::find(execution_order.begin(), execution_order.end(), id);
        };
        const auto position_a = position("job-A");
        const auto position_b = position("job-B");
        const auto position_c = position("job-C");
        const auto position_d = position("job-D");
        const auto position_e = position("job-E");
        const auto occurs_once = [&](const std::string& id) {
            return std::count(execution_order.begin(), execution_order.end(), id) == 1;
        };

        EXPECT_EQ(execution_order.size(), 5);
        EXPECT_TRUE(occurs_once("job-A"));
        EXPECT_TRUE(occurs_once("job-B"));
        EXPECT_TRUE(occurs_once("job-C"));
        EXPECT_TRUE(occurs_once("job-D"));
        EXPECT_TRUE(occurs_once("job-E"));
        EXPECT_NE(position_e, execution_order.end());
        EXPECT_LT(position_a, position_c);
        EXPECT_LT(position_b, position_c);
        EXPECT_LT(position_c, position_d);
    }
}

TEST(Scheduler, PriorityOrderingPreservesCategoryAndInsertionPriority) {
    std::mutex order_mutex;
    std::vector<std::string> priority_order;
    auto record_priority_order = [&](const std::string& id) {
        std::lock_guard<std::mutex> lock(order_mutex);
        priority_order.push_back(id);
    };

    StubJob jobA("job-A", 3, record_priority_order);
    StubJob jobB("job-B", 3, record_priority_order);
    StubJob jobC("job-C", 3, record_priority_order);
    StubJob jobD("job-D", 3, record_priority_order);
    StubJob jobE("job-E", 3, record_priority_order);

    {
        Scheduler scheduler(1);
        scheduler.add_job(&jobA, 1, 5);
        scheduler.add_job(&jobB, 2, 4);
        scheduler.add_job(&jobC, 2, 5);
        scheduler.add_job(&jobD, 1, 3);
        scheduler.add_job(&jobE, 2, 3);
        scheduler.start();

        EXPECT_THROW(scheduler.start(), std::logic_error);
        EXPECT_THROW(scheduler.add_job(&jobA, 1, 1), std::logic_error);
        EXPECT_THROW(scheduler.add_dependency("job-A", "job-B"), std::logic_error);
    }

    EXPECT_EQ(priority_order, (std::vector<std::string>{"job-C", "job-B", "job-E", "job-A", "job-D"}));
}

TEST(Scheduler, ThrowingJobsFailWithoutStoppingWorkersAndSkipDependents) {
    std::vector<std::string> executed_jobs;
    std::mutex executed_jobs_mutex;
    auto record_execution = [&](const std::string& id) {
        std::lock_guard<std::mutex> lock(executed_jobs_mutex);
        executed_jobs.push_back(id);
    };

    ThrowingJob throwing_job("throwing-job");
    StubJob dependent_job("dependent-job", 1, record_execution);
    StubJob downstream_job("downstream-job", 1, record_execution);
    StubJob independent_job("independent-job", 1, record_execution);

    {
        Scheduler scheduler(2);
        scheduler.add_job(&throwing_job, 1, 1);
        scheduler.add_job(&dependent_job, 1, 1);
        scheduler.add_job(&downstream_job, 1, 1);
        scheduler.add_job(&independent_job, 1, 1);
        scheduler.add_dependency("throwing-job", "dependent-job");
        scheduler.add_dependency("dependent-job", "downstream-job");
        scheduler.start();
    }

    EXPECT_EQ(executed_jobs, std::vector<std::string>{"independent-job"});
}

TEST(Scheduler, CallbackExceptionsDoNotTerminateWorkerThreads) {
    StubJob job("callback-exception-job", 1);
    bool callback_invoked = false;
    {
        ThreadPool pool(1, [&](const std::string&, Job_Status) -> FinishedJobs {
            callback_invoked = true;
            throw std::runtime_error("expected callback exception");
        });
        pool.push_job(&job, 1, 1);
    }

    EXPECT_TRUE(callback_invoked);
}

TEST(Scheduler, CyclicGraphIsRejectedBeforeScheduling) {
    StubJob cyclic_job_a("cyclic-A", 3);
    StubJob cyclic_job_b("cyclic-B", 3);
    Scheduler cyclic_scheduler(1);
    cyclic_scheduler.add_job(&cyclic_job_a, 1, 1);
    cyclic_scheduler.add_job(&cyclic_job_b, 1, 1);
    cyclic_scheduler.add_dependency("cyclic-A", "cyclic-B");
    cyclic_scheduler.add_dependency("cyclic-B", "cyclic-A");

    EXPECT_THROW(cyclic_scheduler.start(), std::runtime_error);
}
