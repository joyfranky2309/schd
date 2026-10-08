#pragma once
#include <chrono>
#include <optional>
#include <string>
#include "job_status.hpp"

class JobRun {
public:
    explicit JobRun(std::string job_id);

    const std::string& job_id() const noexcept;
    Job_Status status() const noexcept;
    const std::optional<std::chrono::system_clock::time_point>& start_time() const noexcept;
    const std::optional<std::chrono::system_clock::time_point>& end_time() const noexcept;

    void transition_to(Job_Status new_status);

private:
    std::string job_id_;
    Job_Status status_;
    std::optional<std::chrono::system_clock::time_point> start_time_;
    std::optional<std::chrono::system_clock::time_point> end_time_;
};