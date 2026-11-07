#include "StubJob.hpp"
#include <iostream>
#include <utility>

StubJob::StubJob(
    std::string id,
    int max_attempts,
    std::function<void(const std::string&)> on_execute)
    : Job(std::move(id), max_attempts), on_execute_(std::move(on_execute)) {}

Job_Status StubJob::execute() {
    if (on_execute_) {
        on_execute_(id());
    }
    std::cout << "Executing StubJob with id: " << id() << '\n';
    return Job_Status::SUCCESS;
}