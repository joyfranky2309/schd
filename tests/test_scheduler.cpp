#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "failing_stub_job.hpp"
#include "scheduler.hpp"
#include "stub_job.hpp"

namespace {
class ThrowingStubJob : public Job {
public:
    explicit ThrowingStubJob(std::string id) : Job(std::move(id), 1) {}

    ExecResult execute() override {
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
        scheduler.add_job(&jobB, 2, 1);
        scheduler.add_job(&jobC, 2, 5);
        scheduler.add_job(&jobD, 1, 3);
        scheduler.add_job(&jobE, 2, 1);
        scheduler.start();

        EXPECT_THROW(scheduler.start(), std::logic_error);
        EXPECT_THROW(scheduler.add_job(&jobA, 1, 1), std::logic_error);
        EXPECT_THROW(scheduler.add_dependency("job-A", "job-B"), std::logic_error);
    }

    EXPECT_EQ(priority_order, (std::vector<std::string>{"job-C", "job-B", "job-E", "job-A", "job-D"}));
}

TEST(Scheduler, TwentyEqualPriorityJobsRunInRegistrationOrder) {
    std::vector<std::string> execution_order;
    auto record_execution = [&](const std::string& id) {
        execution_order.push_back(id);
    };

    std::vector<std::unique_ptr<StubJob>> jobs;
    jobs.reserve(20);
    for (int i = 0; i < 20; ++i) {
        jobs.push_back(std::make_unique<StubJob>(
            "job-" + std::to_string(i), 1, record_execution));
    }

    std::vector<std::string> expected_order;
    expected_order.reserve(jobs.size());
    {
        Scheduler scheduler(1);
        for (const auto& job : jobs) {
            expected_order.push_back(job->id());
            scheduler.add_job(job.get(), 1, 1);
        }
        scheduler.start();
    }

    EXPECT_EQ(execution_order, expected_order);
}

TEST(Scheduler, ThrowingJobsFailWithoutStoppingWorkersAndSkipDependents) {
    std::vector<std::string> executed_jobs;
    std::mutex executed_jobs_mutex;
    auto record_execution = [&](const std::string& id) {
        std::lock_guard<std::mutex> lock(executed_jobs_mutex);
        executed_jobs.push_back(id);
    };

    ThrowingStubJob throwing_job("throwing-job");
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
        scheduler.wait();

        EXPECT_EQ(scheduler.final_status("throwing-job"), Job_Status::FAILED);
        EXPECT_EQ(scheduler.final_status("dependent-job"), Job_Status::SKIPPED);
        EXPECT_EQ(scheduler.final_status("downstream-job"), Job_Status::SKIPPED);
        EXPECT_EQ(scheduler.final_status("independent-job"), Job_Status::SUCCESS);
    }

    EXPECT_EQ(executed_jobs, std::vector<std::string>{"independent-job"});
}

TEST(Scheduler, WaitRequiresStartAndFinalStatusRequiresWait) {
    StubJob job("wait-guard-job", 1);
    Scheduler scheduler(1);

    EXPECT_THROW(scheduler.wait(), std::logic_error);
    scheduler.add_job(&job, 1, 1);
    scheduler.start();
    EXPECT_THROW(scheduler.final_status("wait-guard-job"), std::logic_error);
    scheduler.wait();

    EXPECT_EQ(scheduler.final_status("wait-guard-job"), Job_Status::SUCCESS);
    EXPECT_THROW(scheduler.final_status("missing-job"), std::out_of_range);
}

TEST(Scheduler, WaitCompletesForAllSuccessAndAllFailureRuns) {
    StubJob success_a("success-A", 1);
    StubJob success_b("success-B", 1);
    {
        Scheduler scheduler(2);
        scheduler.add_job(&success_a, 1, 1);
        scheduler.add_job(&success_b, 1, 1);
        scheduler.start();
        scheduler.wait();

        EXPECT_EQ(scheduler.final_status("success-A"), Job_Status::SUCCESS);
        EXPECT_EQ(scheduler.final_status("success-B"), Job_Status::SUCCESS);
    }

    FailingStubJob failure_a("failure-A", 1);
    FailingStubJob failure_b("failure-B", 1);
    {
        Scheduler scheduler(2);
        scheduler.add_job(&failure_a, 1, 1);
        scheduler.add_job(&failure_b, 1, 1);
        scheduler.start();
        scheduler.wait();

        EXPECT_EQ(scheduler.final_status("failure-A"), Job_Status::FAILED);
        EXPECT_EQ(scheduler.final_status("failure-B"), Job_Status::FAILED);
    }
}

TEST(Scheduler, MixedChainRunSkipsDependentsAndKeepsIndependentJobRunning) {
    for (int iteration = 0; iteration < 50; ++iteration) {
        std::vector<std::string> executed_jobs;
        FailingStubJob job_a("job-A", 1);
        StubJob job_b("job-B", 1, [&](const std::string& id) {
            executed_jobs.push_back(id);
        });
        StubJob job_c("job-C", 1, [&](const std::string& id) {
            executed_jobs.push_back(id);
        });
        StubJob independent_job("job-E", 1, [&](const std::string& id) {
            executed_jobs.push_back(id);
        });

        Scheduler scheduler(3);
        scheduler.add_job(&job_a, 1, 1);
        scheduler.add_job(&job_b, 1, 1);
        scheduler.add_job(&job_c, 1, 1);
        scheduler.add_job(&independent_job, 1, 1);
        scheduler.add_dependency("job-A", "job-B");
        scheduler.add_dependency("job-B", "job-C");
        scheduler.start();
        scheduler.wait();

        EXPECT_EQ(scheduler.final_status("job-A"), Job_Status::FAILED);
        EXPECT_EQ(scheduler.final_status("job-B"), Job_Status::SKIPPED);
        EXPECT_EQ(scheduler.final_status("job-C"), Job_Status::SKIPPED);
        EXPECT_EQ(scheduler.final_status("job-E"), Job_Status::SUCCESS);
        EXPECT_EQ(executed_jobs, std::vector<std::string>{"job-E"});
    }
}

TEST(Scheduler, DiamondSkipsSharedDependentEvenWhenOtherParentSucceeds) {
    std::atomic<int> dependent_execution_count{0};
    FailingStubJob job_a("job-A", 1);
    StubJob job_b("job-B", 1);
    StubJob job_c("job-C", 1, [&](const std::string&) {
        ++dependent_execution_count;
    });

    Scheduler scheduler(2);
    scheduler.add_job(&job_a, 1, 1);
    scheduler.add_job(&job_b, 1, 1);
    scheduler.add_job(&job_c, 1, 1);
    scheduler.add_dependency("job-A", "job-C");
    scheduler.add_dependency("job-B", "job-C");
    scheduler.start();
    scheduler.wait();

    EXPECT_EQ(scheduler.final_status("job-A"), Job_Status::FAILED);
    EXPECT_EQ(scheduler.final_status("job-B"), Job_Status::SUCCESS);
    EXPECT_EQ(scheduler.final_status("job-C"), Job_Status::SKIPPED);
    EXPECT_EQ(dependent_execution_count.load(), 0);
}

TEST(Scheduler, FailureSkipsWideFanOutExactlyOnce) {
    constexpr int dependent_count = 20;
    std::atomic<int> dependent_execution_count{0};
    FailingStubJob failed_job("failed-job", 1);
    std::vector<std::unique_ptr<StubJob>> dependents;
    dependents.reserve(dependent_count);
    for (int i = 0; i < dependent_count; ++i) {
        dependents.push_back(std::make_unique<StubJob>(
            "dependent-" + std::to_string(i),
            1,
            [&](const std::string&) { ++dependent_execution_count; }));
    }

    Scheduler scheduler(4);
    scheduler.add_job(&failed_job, 1, 1);
    for (const auto& dependent : dependents) {
        scheduler.add_job(dependent.get(), 1, 1);
        scheduler.add_dependency(failed_job.id(), dependent->id());
    }
    scheduler.start();
    scheduler.wait();

    EXPECT_EQ(scheduler.final_status(failed_job.id()), Job_Status::FAILED);
    for (const auto& dependent : dependents) {
        EXPECT_EQ(scheduler.final_status(dependent->id()), Job_Status::SKIPPED);
    }
    EXPECT_EQ(dependent_execution_count.load(), 0);
}

TEST(ThreadPool, ThrowingJobPreservesExceptionMessageInResult) {
    ThrowingStubJob job("throwing-result-job");
    ExecResult observed_result{Job_Status::PENDING};
    bool callback_invoked = false;
    {
        ThreadPool pool(1, [&](const std::string&, const ExecResult& result) {
            observed_result = result;
            callback_invoked = true;
            return FinishedJobs{};
        });
        pool.push_job(&job, 1, 1);
    }

    ASSERT_TRUE(callback_invoked);
    EXPECT_EQ(observed_result.status, Job_Status::FAILED);
    EXPECT_EQ(observed_result.exit_code, -1);
    EXPECT_EQ(observed_result.message, "expected test exception");
}

TEST(Scheduler, CallbackExceptionsDoNotTerminateWorkerThreads) {
    StubJob first_job("callback-exception-job-1", 1);
    StubJob second_job("callback-exception-job-2", 1);
    int callback_count = 0;
    {
        ThreadPool pool(1, [&](const std::string&, const ExecResult&) -> FinishedJobs {
            ++callback_count;
            throw std::runtime_error("expected callback exception");
        });
        pool.push_jobs({
            ReadyJob{&first_job, 1, 1},
            ReadyJob{&second_job, 1, 1}});
    }

    EXPECT_EQ(callback_count, 2);
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
