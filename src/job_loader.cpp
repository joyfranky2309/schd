#include "job_loader.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "shell_job.hpp"
#include "cpu_pricing_job.hpp"

namespace {
using Json = nlohmann::json;

struct ParsedJob {
    std::string id;
    std::string type;
    std::string command;
    std::string settlement_date;
    double face_value{100.0};
    double coupon_rate{0.0};
    int maturity_years{1};
    double yield{0.0};
    int category;
    int priority;
    int max_attempts;
    std::vector<std::string> dependencies;
};

[[noreturn]] void job_error(const std::string& id, const std::string& reason) {
    throw std::runtime_error("Job '" + id + "': " + reason);
}

std::string job_id_for_error(const Json& value, std::size_t index) {
    if (value.is_object() && value.contains("id") && value["id"].is_string()) {
        const auto id = value["id"].get<std::string>();
        if (!id.empty()) {
            return id;
        }
    }
    return "at index " + std::to_string(index);
}

std::string required_string(const Json& value, const char* field, const std::string& id) {
    const auto it = value.find(field);
    if (it == value.end() || !it->is_string()) {
        job_error(id, std::string("'") + field + "' must be a string");
    }
    return it->get<std::string>();
}

int optional_integer(
    const Json& value,
    const char* field,
    int default_value,
    const std::string& id)
{
    const auto it = value.find(field);
    if (it == value.end()) {
        return default_value;
    }
    if (!it->is_number_integer()) {
        job_error(id, std::string("'") + field + "' must be an integer");
    }
    if (it->is_number_unsigned()) {
        const std::uint64_t number = it->get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            job_error(id, std::string("'") + field + "' is outside the supported range");
        }
        return static_cast<int>(number);
    }
    const std::int64_t number = it->get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        job_error(id, std::string("'") + field + "' is outside the supported range");
    }
    return static_cast<int>(number);
}

double required_number(const Json& value, const char* field, const std::string& id) {
    const auto it = value.find(field);
    if (it == value.end() || !it->is_number()) {
        job_error(id, std::string("'params.") + field + "' must be a number");
    }
    const double number = it->get<double>();
    if (!std::isfinite(number)) {
        job_error(id, std::string("'params.") + field + "' must be finite");
    }
    return number;
}

std::size_t workers_value(const Json& value) {
    if (!value.contains("workers")) {
        return 7;
    }
    const auto& workers = value["workers"];
    if (!workers.is_number_integer()) {
        throw std::runtime_error("'workers' must be a positive integer");
    }
    if (workers.is_number_unsigned()) {
        const std::uint64_t number = workers.get<std::uint64_t>();
        if (number == 0 ||
            number > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("'workers' must be a positive integer");
        }
        return static_cast<std::size_t>(number);
    }
    const std::int64_t number = workers.get<std::int64_t>();
    if (number < 1 ||
        static_cast<std::uint64_t>(number) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("'workers' must be a positive integer");
    }
    return static_cast<std::size_t>(number);
}
}

LoadedJobFile load_job_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open job file '" + path.string() + "'");
    }

    Json document;
    try {
        input >> document;
    } catch (const Json::exception& error) {
        throw std::runtime_error(
            "Invalid JSON in job file '" + path.string() + "': " + error.what());
    }

    if (!document.is_object()) {
        throw std::runtime_error("Job file root must be an object");
    }
    const std::size_t workers = workers_value(document);
    const auto jobs_it = document.find("jobs");
    if (jobs_it == document.end() || !jobs_it->is_array()) {
        throw std::runtime_error("'jobs' must be an array");
    }

    std::vector<ParsedJob> parsed_jobs;
    parsed_jobs.reserve(jobs_it->size());
    std::unordered_set<std::string> ids;

    for (std::size_t index = 0; index < jobs_it->size(); ++index) {
        const auto& value = (*jobs_it)[index];
        const std::string id = job_id_for_error(value, index);
        if (!value.is_object()) {
            job_error(id, "entry must be an object");
        }
        if (!value.contains("id") || !value["id"].is_string() ||
            value["id"].get<std::string>().empty()) {
            job_error(id, "'id' must be a non-empty string");
        }
        const std::string actual_id = value["id"].get<std::string>();
        if (!ids.insert(actual_id).second) {
            job_error(actual_id, "duplicate job id");
        }

        const std::string type = required_string(value, "type", actual_id);
        if (type != "shell" && type != "cpu_pricing") {
            job_error(actual_id, "unknown job type '" + type + "'");
        }

        std::string command;
        if (type == "shell") {
            command = required_string(value, "command", actual_id);
        }

        std::string settlement_date = "2024-01-02";
        double face_value = 100.0;
        double coupon_rate = 0.0;
        int maturity_years = 1;
        double yield = 0.0;
        if (type == "cpu_pricing") {
            const auto params_it = value.find("params");
            if (params_it == value.end() || !params_it->is_object()) {
                job_error(actual_id, "'params' must be an object");
            }
            face_value = required_number(*params_it, "face_value", actual_id);
            coupon_rate = required_number(*params_it, "coupon_rate", actual_id);
            if (!params_it->contains("maturity_years") ||
                !(*params_it)["maturity_years"].is_number_integer()) {
                job_error(actual_id, "'params.maturity_years' must be an integer");
            }
            maturity_years = optional_integer(
                *params_it, "maturity_years", 1, actual_id);
            yield = required_number(*params_it, "yield", actual_id);
            if (face_value <= 0.0) {
                job_error(actual_id, "'params.face_value' must be positive");
            }
            if (maturity_years < 1) {
                job_error(actual_id, "'params.maturity_years' must be at least 1");
            }
            const auto settlement_it = params_it->find("settlement_date");
            if (settlement_it != params_it->end()) {
                if (!settlement_it->is_string()) {
                    job_error(actual_id, "'params.settlement_date' must be a string");
                }
                settlement_date = settlement_it->get<std::string>();
            }
        }
        const std::string category_name = required_string(value, "category", actual_id);
        int category = 0;
        if (category_name == "EOD") {
            category = 2;
        } else if (category_name == "ADHOC") {
            category = 1;
        } else {
            job_error(actual_id, "category must be 'EOD' or 'ADHOC'");
        }

        const int priority = optional_integer(value, "priority", 1, actual_id);
        const int max_attempts = optional_integer(value, "max_attempts", 1, actual_id);
        if (max_attempts < 1) {
            job_error(actual_id, "'max_attempts' must be at least 1");
        }

        std::vector<std::string> dependencies;
        const auto dependencies_it = value.find("depends_on");
        if (dependencies_it != value.end()) {
            if (!dependencies_it->is_array()) {
                job_error(actual_id, "'depends_on' must be an array of job ids");
            }
            dependencies.reserve(dependencies_it->size());
            for (const auto& dependency : *dependencies_it) {
                if (!dependency.is_string() || dependency.get<std::string>().empty()) {
                    job_error(actual_id, "'depends_on' entries must be non-empty strings");
                }
                dependencies.push_back(dependency.get<std::string>());
            }
        }

        parsed_jobs.push_back(ParsedJob{
            actual_id,
            type,
            command,
            settlement_date,
            face_value,
            coupon_rate,
            maturity_years,
            yield,
            category,
            priority,
            max_attempts,
            std::move(dependencies)});
    }

    for (const auto& parsed_job : parsed_jobs) {
        for (const auto& dependency : parsed_job.dependencies) {
            if (ids.find(dependency) == ids.end()) {
                job_error(
                    parsed_job.id,
                    "depends on unknown job '" + dependency + "'");
            }
        }
    }

    LoadedJobFile loaded;
    loaded.workers = workers;
    loaded.jobs.reserve(parsed_jobs.size());
    loaded.registrations.reserve(parsed_jobs.size());
    for (const auto& parsed_job : parsed_jobs) {
        std::unique_ptr<Job> job;
        if (parsed_job.type == "shell") {
            job = std::make_unique<ShellJob>(
                parsed_job.id, parsed_job.command, parsed_job.max_attempts);
        } else {
            job = std::make_unique<CpuPricingJob>(
                parsed_job.id,
                parsed_job.face_value,
                parsed_job.coupon_rate,
                parsed_job.maturity_years,
                parsed_job.yield,
                parsed_job.settlement_date,
                parsed_job.max_attempts);
        }
        loaded.registrations.push_back(JobRegistration{
            job.get(),
            parsed_job.category,
            parsed_job.priority,
            parsed_job.dependencies});
        loaded.jobs.push_back(std::move(job));
    }
    return loaded;
}
