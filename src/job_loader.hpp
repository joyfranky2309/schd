#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "job.hpp"

struct JobRegistration {
    Job* job;
    int category;
    int priority;
    std::vector<std::string> dependencies;
};

struct LoadedJobFile {
    std::vector<std::unique_ptr<Job>> jobs;
    std::vector<JobRegistration> registrations;
    std::size_t workers{7};
};

LoadedJobFile load_job_file(const std::filesystem::path& path);
