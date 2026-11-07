#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "dag.hpp"
#include "failing_stub_job.hpp"
#include "job_run.hpp"
#include "job_status.hpp"
#include "scheduler.hpp"
#include "StubJob.hpp"

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
    if (cyclic_dag_rejected) {
        std::cerr << "ERROR: refusing to start a cyclic DAG\n";
    }
    all_passed &= report_test(
        "cyclic DAG is rejected before scheduling",
        cyclic_dag_rejected);

    bool dependency_order_passed = true;
    for (int iteration = 0; iteration < 30; ++iteration) {
        std::mutex execution_order_mutex;
        std::vector<std::string> execution_order;
        auto record_execution = [&](const std::string& id) {
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