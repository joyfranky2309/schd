#include "job_run.hpp"

#include <stdexcept>
#include <utility>

JobRun::JobRun(std::string job_id)
    : job_id_(std::move(job_id)), status_(Job_Status::PENDING) {}

const std::string& JobRun::job_id() const noexcept {
    return job_id_;
}

Job_Status JobRun::status() const noexcept {
    return status_;
}

const std::optional<std::chrono::system_clock::time_point>& JobRun::start_time() const noexcept {
    return start_time_;
}

const std::optional<std::chrono::system_clock::time_point>& JobRun::end_time() const noexcept {
    return end_time_;
}

void JobRun::transition_to(Job_Status new_status) {
    if (!can_transition(status_, new_status)) {
        throw std::runtime_error(
            "Illegal job status transition from " + std::string(to_string(status_)) +
            " to " + std::string(to_string(new_status)));
    }

    const auto now = std::chrono::system_clock::now();
    if (new_status == Job_Status::RUNNING) {
        start_time_ = now;
    }
    if (new_status == Job_Status::SUCCESS || new_status == Job_Status::FAILED ||
        new_status == Job_Status::SKIPPED) {
        end_time_ = now;
    }
    status_ = new_status;
}