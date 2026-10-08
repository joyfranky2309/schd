// job_run.hpp
#pragma once
#include <iostream>
#include <chrono>
#include <string>
#include <stdexcept>
#include "job_status.hpp"

class JobRun {
public:
    explicit JobRun(std::string job_id)
        : job_id_(std::move(job_id)), status_(Job_Status::PENDING) {}

    const std::string& job_id() const { return job_id_; }
    Job_Status status() const { return status_; }

    void transition_to(Job_Status new_status) {
        if(!can_transition(status_,new_status))
        {
            // std::cout<<"Illegal transition from "<<to_string(status_)<<" to "<<to_string(new_status)<<std::endl;
            throw std::runtime_error("Illegal transition");
        }
        status_ = new_status;
    }

private:
    std::string job_id_;
    Job_Status status_;
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    int exit_code_ = 0;
};