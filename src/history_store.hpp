#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scheduler.hpp"

struct sqlite3;

struct HistoryRow {
    std::string run_id;
    std::string job_id;
    int attempt;
    std::string status;
    std::int64_t queued_ms;
    std::int64_t started_ms;
    std::int64_t ended_ms;
    int exit_code;
    std::string message;
};

class HistoryStore {
public:
    explicit HistoryStore(const std::string& path);
    ~HistoryStore();

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;
    HistoryStore(HistoryStore&& other) noexcept;
    HistoryStore& operator=(HistoryStore&& other) noexcept;

    void save_run(const std::string& run_id,
                  const std::vector<RunRecord>& records);
    std::vector<HistoryRow> history(const std::string* job_id, int limit) const;

private:
    sqlite3* database_;
};

std::string generate_run_id();
