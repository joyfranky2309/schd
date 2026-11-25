#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "exec_result.hpp"
#include "job.hpp"
#include "scheduler.hpp"

namespace {
constexpr std::size_t kJobCount = 1000;
constexpr std::size_t kLayerCount = 20;
constexpr std::size_t kJobsPerLayer = kJobCount / kLayerCount;
constexpr unsigned int kSeed = 0x5c1d5eedU;

class NoopJob : public Job {
public:
    explicit NoopJob(std::string id) : Job(std::move(id), 1) {}
    ExecResult execute() override {
        return Job_Status::SUCCESS;
    }
};

class SleepJob : public Job {
public:
    explicit SleepJob(std::string id) : Job(std::move(id), 1) {}
    ExecResult execute() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return Job_Status::SUCCESS;
    }
};

struct Edge {
    std::size_t from;
    std::size_t to;
};

std::vector<Edge> generate_dag() {
    std::vector<Edge> edges;
    edges.reserve(kJobCount * 3);
    std::mt19937 random(kSeed);
    for (std::size_t layer = 1; layer < kLayerCount; ++layer) {
        for (std::size_t offset = 0; offset < kJobsPerLayer; ++offset) {
            const std::size_t to = layer * kJobsPerLayer + offset;
            std::uniform_int_distribution<std::size_t> dependency_count(1, 3);
            const std::size_t count = dependency_count(random);
            std::vector<std::size_t> previous_layer;
            for (std::size_t previous = (layer - 1) * kJobsPerLayer;
                 previous < layer * kJobsPerLayer; ++previous) {
                previous_layer.push_back(previous);
            }
            std::shuffle(previous_layer.begin(), previous_layer.end(), random);
            for (std::size_t dependency = 0; dependency < count; ++dependency) {
                edges.push_back(Edge{previous_layer[dependency], to});
            }
        }
    }
    return edges;
}

template <typename JobType>
double run_once(const std::vector<Edge>& edges, std::size_t workers) {
    std::vector<std::unique_ptr<JobType>> jobs;
    jobs.reserve(kJobCount);
    for (std::size_t i = 0; i < kJobCount; ++i) {
        jobs.push_back(std::make_unique<JobType>("job-" + std::to_string(i)));
    }

    Scheduler scheduler(workers);
    for (const auto& job : jobs) {
        scheduler.add_job(job.get(), 1, 1);
    }
    for (const Edge& edge : edges) {
        scheduler.add_dependency(
            "job-" + std::to_string(edge.from),
            "job-" + std::to_string(edge.to));
    }

    const auto start = std::chrono::steady_clock::now();
    scheduler.start();
    scheduler.wait();
    const auto finish = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(finish - start).count();
}

double median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

template <typename JobType>
double benchmark(
    const std::vector<Edge>& edges,
    std::size_t workers,
    std::vector<double>& per_worker_ms) {
    std::vector<double> samples;
    samples.reserve(5);
    for (int run = 0; run < 5; ++run) {
        samples.push_back(run_once<JobType>(edges, workers));
    }
    const double wall_ms = median(samples);
    per_worker_ms.push_back(wall_ms);
    return wall_ms;
}
}

int main() {
    std::cout << "hardware_concurrency: " << std::thread::hardware_concurrency() << '\n';
    const auto edges = generate_dag();
    const std::vector<std::size_t> worker_counts{1, 2, 4, 7};

    std::cout << "\n| variant | workers | wall ms | jobs/sec | speedup vs 1 worker | scheduling overhead us/job |\n";
    std::cout << "|---|---:|---:|---:|---:|---:|\n";

    std::vector<double> noop_ms;
    for (const std::size_t workers : worker_counts) {
        const double wall_ms = benchmark<NoopJob>(edges, workers, noop_ms);
        const double jobs_per_second = kJobCount * 1000.0 / wall_ms;
        const double speedup = noop_ms.front() / wall_ms;
        const double overhead_us = wall_ms * 1000.0 / kJobCount;
        std::cout << "| no-op | " << workers << " | " << std::fixed
                  << std::setprecision(3) << wall_ms << " | "
                  << std::setprecision(1) << jobs_per_second << " | "
                  << std::setprecision(3) << speedup << " | "
                  << std::setprecision(3) << overhead_us << " |\n";
    }

    std::vector<double> sleep_ms;
    for (const std::size_t workers : worker_counts) {
        const double wall_ms = benchmark<SleepJob>(edges, workers, sleep_ms);
        const double jobs_per_second = kJobCount * 1000.0 / wall_ms;
        const double speedup = sleep_ms.front() / wall_ms;
        std::cout << "| sleep-1ms | " << workers << " | " << std::fixed
                  << std::setprecision(3) << wall_ms << " | "
                  << std::setprecision(1) << jobs_per_second << " | "
                  << std::setprecision(3) << speedup << " | - |\n";
    }

    return 0;
}
