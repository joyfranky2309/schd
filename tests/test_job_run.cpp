#include <gtest/gtest.h>

#include <stdexcept>

#include "failing_stub_job.hpp"
#include "job_run.hpp"
#include "stub_job.hpp"

namespace {
JobRun execute_job(Job& job) {
    JobRun run(job.id());
    run.transition_to(Job_Status::QUEUED);
    run.transition_to(Job_Status::RUNNING);
    run.transition_to(job.execute());
    return run;
}
}

TEST(JobRun, SuccessfulJobEndsInSuccessWithTimestamps) {
    StubJob successful_job("job-1", 3);
    const JobRun successful_run = execute_job(successful_job);

    EXPECT_EQ(successful_run.status(), Job_Status::SUCCESS);
    EXPECT_TRUE(successful_run.start_time().has_value());
    EXPECT_TRUE(successful_run.end_time().has_value());
}

TEST(JobRun, FailingJobEndsInFailedWithTimestamps) {
    FailingStubJob failing_job("job-2", 3);
    const JobRun failing_run = execute_job(failing_job);

    EXPECT_EQ(failing_run.status(), Job_Status::FAILED);
    EXPECT_TRUE(failing_run.start_time().has_value());
    EXPECT_TRUE(failing_run.end_time().has_value());
}

TEST(JobRun, IllegalPendingToRunningTransitionIsRejected) {
    JobRun invalid_run("job-invalid");

    EXPECT_THROW(invalid_run.transition_to(Job_Status::RUNNING), std::runtime_error);
    EXPECT_EQ(invalid_run.status(), Job_Status::PENDING);
}
