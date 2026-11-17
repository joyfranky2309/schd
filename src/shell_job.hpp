#pragma once

#include <string>

#include "job.hpp"

class ShellJob : public Job {
public:
    ShellJob(std::string id, std::string command, int max_attempts);

    ExecResult execute() override;

private:
    std::string command_;
};
