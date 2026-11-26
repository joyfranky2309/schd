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

#ifdef SCHD_WITH_SQLITE
#include "history_store.hpp"
#endif

namespace {
void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  schd run <file> [--workers N] [--verbose] [--db <path>]\n"
           << "  schd validate <file>\n"
           << "  schd history <db> [--job ID] [--limit N]\n";
}

std::size_t parse_worker_count(std::string_view value) {
    std::size_t workers = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), workers);
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

int run_command(const std::string& path, const std::optional<std::size_t>& worker_override,
                bool verbose, const std::optional<std::string>& db_path) {
#ifdef SCHD_WITH_SQLITE
    std::optional<HistoryStore> history_store;
    std::string run_id;
    if (db_path) {
        history_store.emplace(*db_path);
        run_id = generate_run_id();
    }
#else
    if (db_path) {
        throw std::runtime_error("schd is built without SCHD_WITH_SQLITE");
    }
#endif
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

#ifdef SCHD_WITH_SQLITE
    if (history_store) {
        std::vector<RunRecord> run_records;
        for (const auto& registration : job_file.registrations) {
            const auto records = scheduler.history(registration.job->id());
            run_records.insert(run_records.end(), records.begin(), records.end());
        }
        try {
            history_store->save_run(run_id, run_records);
        } catch (const std::exception& error) {
            std::cerr << "schd: could not write run history: " << error.what() << '\n';
            return 1;
        }
    }
#endif

    std::cout << std::left << std::setw(24) << "ID" << std::setw(12) << "STATUS" << std::right
              << std::setw(10) << "ATTEMPTS" << std::setw(16) << "DURATION_MS" << '\n';
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
        std::cout << std::left << std::setw(24) << id << std::setw(12) << to_string(status)
                  << std::right << std::setw(10) << attempts << std::setw(16) << duration.count()
                  << '\n';
        if (status != Job_Status::SUCCESS) {
            all_succeeded = false;
        }
    }
    if (verbose) {
        for (const auto& registration : job_file.registrations) {
            const auto records = scheduler.history(registration.job->id());
            std::cout << registration.job->id() << ": " << records.back().message << '\n';
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

    if (command == "history") {
#ifndef SCHD_WITH_SQLITE
        throw std::runtime_error("schd is built without SCHD_WITH_SQLITE");
#else
        if (argc < 3) {
            print_usage(std::cerr);
            throw std::invalid_argument("history requires a database path");
        }
        std::optional<std::string> job_filter;
        int limit = 100;
        for (int index = 3; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--job" && index + 1 < argc) {
                if (job_filter) {
                    throw std::invalid_argument("--job may only be specified once");
                }
                job_filter = std::string(argv[++index]);
            } else if (option == "--limit" && index + 1 < argc) {
                const std::string_view value(argv[++index]);
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), limit);
                if (error != std::errc{} || end != value.data() + value.size() || limit < 0) {
                    throw std::invalid_argument("--limit must be a non-negative integer");
                }
            } else {
                print_usage(std::cerr);
                throw std::invalid_argument("expected --job ID or --limit N");
            }
        }
        const HistoryStore store = HistoryStore::open_readonly(argv[2]);
        const std::vector<HistoryRow> rows =
            store.history(job_filter ? &*job_filter : nullptr, limit);
        std::cout << std::left << std::setw(36) << "RUN_ID" << std::setw(24) << "JOB_ID"
                  << std::setw(10) << "ATTEMPT" << std::setw(12) << "STATUS"
                  << std::right << std::setw(12) << "EXIT_CODE" << "  MESSAGE\n";
        for (const HistoryRow& row : rows) {
            std::cout << std::left << std::setw(36) << row.run_id << std::setw(24) << row.job_id
                      << std::setw(10) << row.attempt << std::setw(12) << row.status
                      << std::right << std::setw(12) << row.exit_code << "  " << row.message
                      << '\n';
        }
        return 0;
#endif
    }

    if (command == "run") {
        if (argc < 3) {
            print_usage(std::cerr);
            throw std::invalid_argument("run requires a job file");
        }
        std::optional<std::size_t> worker_override;
        std::optional<std::string> db_path;
        bool verbose = false;
        for (int index = 3; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--verbose") {
                verbose = true;
            } else if (option == "--workers" && index + 1 < argc) {
                if (worker_override.has_value()) {
                    throw std::invalid_argument("--workers may only be specified once");
                }
                worker_override = parse_worker_count(argv[++index]);
            } else if (option == "--db" && index + 1 < argc) {
                if (db_path) {
                    throw std::invalid_argument("--db may only be specified once");
                }
                db_path = std::string(argv[++index]);
            } else {
                print_usage(std::cerr);
                throw std::invalid_argument("expected --workers N, --verbose, or --db <path>");
            }
        }
        return run_command(argv[2], worker_override, verbose, db_path);
    }

    print_usage(std::cerr);
    throw std::invalid_argument("unknown command '" + std::string(command) + "'");
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        return execute_cli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "schd: " << error.what() << '\n';
        return 2;
    }
}
