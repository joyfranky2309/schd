#pragma once
#include "job.hpp"

class FailingStubJob : public Job{
    public: 
    FailingStubJob(std::string id, int max_attempts);
        Job_Status execute() override;
};