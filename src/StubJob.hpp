#pragma once
#include "job.hpp"

class StubJob : public Job {
public:
    StubJob(std::string id, int max_attempts);
    Job_Status execute() override;
};