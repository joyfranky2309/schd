#include <iostream>
#include <stdexcept>
#include "threadpool.hpp"
#include "failing_stub_job.hpp"
#include "job_run.hpp"
#include "job_status.hpp"
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

    {
        StubJob jobA("job-A", 3);
        StubJob jobB("job-B", 3);
        StubJob jobC("job-C", 3);
        StubJob jobD("job-D", 3);
        StubJob jobE("job-E", 3);
        
        ThreadPool pool(1); // Single worker makes the execution order easy to observe.


        pool.push_job(&jobA, 1, 5);
        pool.push_job(&jobB, 2, 1);
        pool.push_job(&jobC, 2, 5);
        pool.push_job(&jobD, 1, 3);
        pool.push_job(&jobE, 2, 1);
    } // The pool drains and joins before the jobs are destroyed.

    return all_passed ? 0 : 1;
}