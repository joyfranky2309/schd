#pragma once

#include <string>
#include <utility>

#include "job_status.hpp"

struct ExecResult {
    Job_Status status;
    int exit_code = 0;
    std::string message;

    ExecResult(Job_Status status, int exit_code = 0, std::string message = {})
        : status(status), exit_code(exit_code), message(std::move(message)) {}
};
