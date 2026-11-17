#include <charconv>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "dag.hpp"
#include "job_loader.hpp"
#include "job_status.hpp"
#include "scheduler.hpp"

namespace {
void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  schd run <file> [--workers N]\n"
           << "  schd validate <file>\n";
}

std::size_t parse_worker_count(std::string_view value) {
    std::size_t workers = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), workers);
    if (error != std::errc{} || end != value.data() + value.size() || workers == 0) {
        throw std::invalid_argument("--workers must be a positive integer");
    }
    return workers;
}

void validate_acyclic(const LoadedJobFile& job_file) {
    DAG dag;
    for (const auto& registration : job_file.registrations) {
        dag.add_job(registration.job->id());
    }
    for (const auto& registration : job_file.registrations) {
        for (const auto& dependency : registration.dependencies) {
            dag.add_edge(dependency, registration.job->id());
        }
    }
    if (dag.has_cycle()) {
        throw std::runtime_error("job graph contains a cycle");
    }
}

int validate_command(const std::string& path) {
    const LoadedJobFile job_file = load_job_file(path);
    validate_acyclic(job_file);
    std::cout << "OK\n";
    return 0;
}

int run_command(
    const std::string& path,
    const std::optional<std::size_t>& worker_override)
{
    LoadedJobFile job_file = load_job_file(path);
    validate_acyclic(job_file);
    const std::size_t workers = worker_override.value_or(job_file.workers);

    Scheduler scheduler(workers);
    for (const auto& registration : job_file.registrations) {
        scheduler.add_job(registration.job, registration.category, registration.priority);
    }
    for (const auto& registration : job_file.registrations) {
        for (const auto& dependency : registration.dependencies) {
            scheduler.add_dependency(dependency, registration.job->id());
        }
    }

    scheduler.start();
    scheduler.wait();

    std::cout << std::left << std::setw(24) << "ID"
              << std::setw(12) << "STATUS"
              << std::right << std::setw(10) << "ATTEMPTS"
              << std::setw(16) << "DURATION_MS" << '\n';
    std::cout << std::string(62, '-') << '\n';

    bool all_succeeded = true;
    for (const auto& registration : job_file.registrations) {
        const auto& id = registration.job->id();
        const auto status = scheduler.final_status(id);
        const auto records = scheduler.history(id);
        int attempts = 0;
        std::chrono::milliseconds duration{0};
        for (const auto& record : records) {
            if (record.attempt > attempts) {
                attempts = record.attempt;
            }
            if (record.started_at && record.finished_at) {
                duration += std::chrono::duration_cast<std::chrono::milliseconds>(
                    *record.finished_at - *record.started_at);
            }
        }
        std::cout << std::left << std::setw(24) << id
                  << std::setw(12) << to_string(status)
                  << std::right << std::setw(10) << attempts
                  << std::setw(16) << duration.count() << '\n';
        if (status != Job_Status::SUCCESS) {
            all_succeeded = false;
        }
    }
    return all_succeeded ? 0 : 1;
}

int execute_cli(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(std::cerr);
        throw std::invalid_argument("missing command");
    }

    const std::string_view command(argv[1]);
    if (command == "validate") {
        if (argc != 3) {
            print_usage(std::cerr);
            throw std::invalid_argument("validate requires exactly one job file");
        }
        return validate_command(argv[2]);
    }

    if (command == "run") {
        if (argc != 3 && argc != 5) {
            print_usage(std::cerr);
            throw std::invalid_argument("run requires a job file and optional --workers N");
        }
        std::optional<std::size_t> worker_override;
        if (argc == 5) {
            if (std::string_view(argv[3]) != "--workers") {
                print_usage(std::cerr);
                throw std::invalid_argument("expected --workers N");
            }
            worker_override = parse_worker_count(argv[4]);
        }
        return run_command(argv[2], worker_override);
    }

    print_usage(std::cerr);
    throw std::invalid_argument("unknown command '" + std::string(command) + "'");
}
}

int main(int argc, char* argv[]) {
    try {
        return execute_cli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "schd: " << error.what() << '\n';
        return 2;
    }
}
