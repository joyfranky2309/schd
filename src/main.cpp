#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "dag.hpp"
#include "failing_stub_job.hpp"
#include "job_run.hpp"
#include "job_status.hpp"
#include "scheduler.hpp"
#include "stub_job.hpp"

namespace {
JobRun execute_job(Job& job) {
    JobRun run(job.id());
    run.transition_to(Job_Status::QUEUED);
    run.transition_to(Job_Status::RUNNING);
    run.transition_to(job.execute());
    return run;
}

bool report_test(const char* name, bool passed) {
    std::cout << (passed ? "PASS: " : "FAIL: ") << name << '\n';
    return passed;
}

class ThrowingJob : public Job {
public:
    explicit ThrowingJob(std::string id) : Job(std::move(id), 1) {}

    Job_Status execute() override {
        throw std::runtime_error("expected test exception");
    }
};
}

int main() {
    bool all_passed = true;

    StubJob successful_job("job-1", 3);
    const JobRun successful_run = execute_job(successful_job);
    all_passed &= report_test(
        "successful job ends in SUCCESS with timestamps",
        successful_run.status() == Job_Status::SUCCESS &&
        successful_run.start_time().has_value() &&
        successful_run.end_time().has_value());

    FailingStubJob failing_job("job-2", 3);
    const JobRun failing_run = execute_job(failing_job);
    all_passed &= report_test(
        "failing job ends in FAILED with timestamps",
        failing_run.status() == Job_Status::FAILED &&
        failing_run.start_time().has_value() &&
        failing_run.end_time().has_value());

    JobRun invalid_run("job-invalid");
    bool rejected_invalid_transition = false;
    try {
        invalid_run.transition_to(Job_Status::RUNNING);
    } catch (const std::runtime_error&) {
        rejected_invalid_transition = true;
    }
    all_passed &= report_test(
        "illegal PENDING-to-RUNNING transition is rejected",
        rejected_invalid_transition && invalid_run.status() == Job_Status::PENDING);

    DAG acyclic_dag;
    acyclic_dag.add_job("standalone-job");
    const bool acyclic_dag_has_cycle = acyclic_dag.has_cycle();
    const auto initially_ready = acyclic_dag_has_cycle
        ? std::vector<std::string>{}
        : acyclic_dag.initial_ready();
    all_passed &= report_test(
        "standalone DAG jobs are initially ready",
        !acyclic_dag_has_cycle &&
        initially_ready == std::vector<std::string>{"standalone-job"});

    DAG cyclic_dag;
    cyclic_dag.add_edge("job-A", "job-B");
    cyclic_dag.add_edge("job-B", "job-A");
    const bool cyclic_dag_rejected = cyclic_dag.has_cycle();
    all_passed &= report_test(
        "cyclic DAG is rejected before scheduling",
        cyclic_dag_rejected);

    bool dependency_order_passed = true;
    for (int iteration = 0; iteration < 30; ++iteration) {
        std::mutex execution_order_mutex;
        std::vector<std::string> execution_order;
        auto record_execution = [&](const std::string& id) {
            thread_local std::mt19937 random_engine(std::random_device{}());
            std::uniform_int_distribution<int> delay(0, 500);
            std::this_thread::sleep_for(
                std::chrono::microseconds(delay(random_engine)));
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
        } // The scheduler drains and joins before the jobs are destroyed.

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
        dependency_order_passed &= execution_order.size() == 5 &&
            occurs_once("job-A") && occurs_once("job-B") &&
            occurs_once("job-C") && occurs_once("job-D") &&
            occurs_once("job-E") && position_e != execution_order.end() &&
            position_a < position_c &&
            position_b < position_c &&
            position_c < position_d;
    }
    all_passed &= report_test(
        "job dependencies are respected across 30 concurrent runs",
        dependency_order_passed);

    {
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

        const std::vector<std::string> expected_order{
            "job-C", "job-B", "job-E", "job-A", "job-D"};
        {
            Scheduler scheduler(1);
            scheduler.add_job(&jobA, 1, 5);
            scheduler.add_job(&jobB, 2, 4);
            scheduler.add_job(&jobC, 2, 5);
            scheduler.add_job(&jobD, 1, 3);
            scheduler.add_job(&jobE, 2, 3);
            scheduler.start();

            bool rejected_second_start = false;
            try {
                scheduler.start();
            } catch (const std::logic_error&) {
                rejected_second_start = true;
            }
            bool rejected_late_job = false;
            try {
                scheduler.add_job(&jobA, 1, 1);
            } catch (const std::logic_error&) {
                rejected_late_job = true;
            }
            bool rejected_late_dependency = false;
            try {
                scheduler.add_dependency("job-A", "job-B");
            } catch (const std::logic_error&) {
                rejected_late_dependency = true;
            }
            all_passed &= report_test(
                "scheduler rejects repeated starts and late configuration",
                rejected_second_start && rejected_late_job &&
                rejected_late_dependency);
        }
        all_passed &= report_test(
            "scheduler preserves priority ordering",
            priority_order == expected_order);
    }

    {
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

        all_passed &= report_test(
            "throwing jobs fail without stopping workers and skip dependents",
            executed_jobs == std::vector<std::string>{"independent-job"});
    }

    {
        StubJob job("callback-exception-job", 1);
        bool callback_invoked = false;
        {
            ThreadPool pool(1, [&](const std::string&, Job_Status) -> FinishedJobs {
                callback_invoked = true;
                throw std::runtime_error("expected callback exception");
            });
            pool.push_job(&job, 1, 1);
        }
        all_passed &= report_test(
            "callback exceptions do not terminate worker threads",
            callback_invoked);
    }

    {
        StubJob cyclic_job_a("cyclic-A", 3);
        StubJob cyclic_job_b("cyclic-B", 3);
        Scheduler cyclic_scheduler(1);
        cyclic_scheduler.add_job(&cyclic_job_a, 1, 1);
        cyclic_scheduler.add_job(&cyclic_job_b, 1, 1);
        cyclic_scheduler.add_dependency("cyclic-A", "cyclic-B");
        cyclic_scheduler.add_dependency("cyclic-B", "cyclic-A");

        bool rejected_cyclic_scheduler = false;
        try {
            cyclic_scheduler.start();
        } catch (const std::runtime_error&) {
            rejected_cyclic_scheduler = true;
        }
        all_passed &= report_test(
            "scheduler rejects a cyclic graph",
            rejected_cyclic_scheduler);
    }

    return all_passed ? 0 : 1;
}