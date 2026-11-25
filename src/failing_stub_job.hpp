#pragma once
#include "job.hpp"

class FailingStubJob : public Job {
public:
    FailingStubJob(std::string id, int max_attempts);
    ExecResult execute() override;
};