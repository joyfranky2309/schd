// job.hpp
#pragma once
#include <string>
#include "job_status.hpp"
class Job {
public:
    Job(std::string id, int max_attempts)
        : id_(std::move(id)), max_attempts_(max_attempts) {}

    virtual ~Job() = default; // base class with virtuals needs a virtual dtor

    virtual Job_Status execute() = 0; // pure virtual — subclasses define *how* the work runs

    const std::string& id() const { return id_; }
    int max_attempts() const { return max_attempts_; }

private:
    std::string id_;
    int max_attempts_;
};