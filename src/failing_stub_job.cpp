#include "failing_stub_job.hpp"
#include <iostream>

FailingStubJob::FailingStubJob(std::string id, int max_attempts)
    : Job(std::move(id), max_attempts) {}

Job_Status FailingStubJob::execute() {
    std::cout << "Executing FailingStubJob with id: " << id() << '\n';
    return Job_Status::FAILED;
}