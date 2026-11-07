#pragma once

#include <functional>
#include "job.hpp"

class StubJob : public Job {
public:
    StubJob(
        std::string id,
        int max_attempts,
        std::function<void(const std::string&)> on_execute = {});
    Job_Status execute() override;

private:
    std::function<void(const std::string&)> on_execute_;
};