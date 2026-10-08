#include "StubJob.hpp"
#include <iostream>

StubJob::StubJob(std::string id, int max_attempts)
    : Job(std::move(id), max_attempts) {}

Job_Status StubJob::execute() {
    std::cout << "Executing StubJob with id: " << id() << '\n';
    return Job_Status::SUCCESS;
}