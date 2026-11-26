#include "history_store.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <sqlite3.h>

#include "job_status.hpp"

namespace {
constexpr char kCreateTable[] =
    "CREATE TABLE IF NOT EXISTS run_records("
    "run_id TEXT, job_id TEXT, attempt INT, status TEXT, "
    "queued_ms INT, started_ms INT, ended_ms INT, exit_code INT, message TEXT)";
constexpr char kInsertRecord[] =
    "INSERT INTO run_records(run_id, job_id, attempt, status, queued_ms, started_ms, "
    "ended_ms, exit_code, message) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)";
constexpr char kHistory[] =
    "SELECT run_id, job_id, attempt, status, queued_ms, started_ms, ended_ms, "
    "exit_code, message FROM run_records ORDER BY rowid DESC LIMIT ?";
constexpr char kHistoryByJob[] =
    "SELECT run_id, job_id, attempt, status, queued_ms, started_ms, ended_ms, "
    "exit_code, message FROM run_records WHERE job_id = ? ORDER BY rowid DESC LIMIT ?";

std::runtime_error sqlite_error(sqlite3* database, const char* operation) {
    return std::runtime_error(std::string(operation) + ": " +
                              sqlite3_errmsg(database));
}

std::int64_t timestamp_ms(
    const std::optional<std::chrono::system_clock::time_point>& timestamp) {
    if (!timestamp) {
        return -1;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               timestamp->time_since_epoch())
        .count();
}

void bind_optional_int64(sqlite3_stmt* statement, int index,
                         std::int64_t value) {
    if (value < 0) {
        sqlite3_bind_null(statement, index);
    } else {
        sqlite3_bind_int64(statement, index, value);
    }
}

std::string column_text(sqlite3_stmt* statement, int column) {
    const unsigned char* text = sqlite3_column_text(statement, column);
    if (!text) {
        return {};
    }
    return reinterpret_cast<const char*>(text);
}
} // namespace

HistoryStore::HistoryStore(const std::string& path) : database_(nullptr) {
    if (sqlite3_open_v2(path.c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        std::runtime_error error = sqlite_error(database_, "could not open SQLite database");
        sqlite3_close(database_);
        database_ = nullptr;
        throw error;
    }

    char* message = nullptr;
    const int result = sqlite3_exec(database_, kCreateTable, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        std::runtime_error error = sqlite_error(database_, "could not create run history table");
        sqlite3_free(message);
        sqlite3_close(database_);
        database_ = nullptr;
        throw error;
    }
}

HistoryStore::~HistoryStore() {
    if (database_) {
        sqlite3_close(database_);
    }
}

HistoryStore::HistoryStore(HistoryStore&& other) noexcept
    : database_(std::exchange(other.database_, nullptr)) {}

HistoryStore& HistoryStore::operator=(HistoryStore&& other) noexcept {
    if (this != &other) {
        if (database_) {
            sqlite3_close(database_);
        }
        database_ = std::exchange(other.database_, nullptr);
    }
    return *this;
}

void HistoryStore::save_run(const std::string& run_id,
                            const std::vector<RunRecord>& records) {
    // HistoryStore is used only after Scheduler::wait(); no scheduler or pool locks are added.
    char* message = nullptr;
    if (sqlite3_exec(database_, "BEGIN", nullptr, nullptr, &message) != SQLITE_OK) {
        std::runtime_error error = sqlite_error(database_, "could not begin history transaction");
        sqlite3_free(message);
        throw error;
    }

    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(database_, kInsertRecord, -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw sqlite_error(database_, "could not prepare history insert");
    }

    for (const RunRecord& record : records) {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        sqlite3_bind_text(statement, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, record.job_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 3, record.attempt);
        const std::string status = std::string(to_string(record.status));
        sqlite3_bind_text(statement, 4, status.c_str(), -1, SQLITE_TRANSIENT);
        bind_optional_int64(statement, 5, timestamp_ms(record.queued_at));
        bind_optional_int64(statement, 6, timestamp_ms(record.started_at));
        bind_optional_int64(statement, 7, timestamp_ms(record.finished_at));
        sqlite3_bind_int(statement, 8, record.exit_code);
        sqlite3_bind_text(statement, 9, record.message.c_str(), -1, SQLITE_TRANSIENT);

        result = sqlite3_step(statement);
        if (result != SQLITE_DONE) {
            sqlite3_finalize(statement);
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
            throw sqlite_error(database_, "could not insert history record");
        }
    }

    sqlite3_finalize(statement);
    if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, &message) != SQLITE_OK) {
        std::runtime_error error = sqlite_error(database_, "could not commit history transaction");
        sqlite3_free(message);
        throw error;
    }
}

std::vector<HistoryRow> HistoryStore::history(const std::string* job_id, int limit) const {
    sqlite3_stmt* statement = nullptr;
    const char* sql = job_id ? kHistoryByJob : kHistory;
    int result = sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr);
    if (result != SQLITE_OK) {
        throw sqlite_error(database_, "could not prepare history query");
    }

    if (job_id) {
        sqlite3_bind_text(statement, 1, job_id->c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 2, limit);
    } else {
        sqlite3_bind_int(statement, 1, limit);
    }

    std::vector<HistoryRow> rows;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        HistoryRow row;
        row.run_id = column_text(statement, 0);
        row.job_id = column_text(statement, 1);
        row.attempt = sqlite3_column_int(statement, 2);
        row.status = column_text(statement, 3);
        row.queued_ms = sqlite3_column_type(statement, 4) == SQLITE_NULL
                            ? -1
                            : sqlite3_column_int64(statement, 4);
        row.started_ms = sqlite3_column_type(statement, 5) == SQLITE_NULL
                             ? -1
                             : sqlite3_column_int64(statement, 5);
        row.ended_ms = sqlite3_column_type(statement, 6) == SQLITE_NULL
                           ? -1
                           : sqlite3_column_int64(statement, 6);
        row.exit_code = sqlite3_column_int(statement, 7);
        row.message = column_text(statement, 8);
        rows.push_back(std::move(row));
    }

    if (result != SQLITE_DONE) {
        sqlite3_finalize(statement);
        throw sqlite_error(database_, "could not read history");
    }
    sqlite3_finalize(statement);
    return rows;
}

std::string generate_run_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto epoch_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::random_device random;
    std::uniform_int_distribution<std::uint32_t> bytes(0, 0xffffffffU);
    std::ostringstream value;
    value << "run-" << epoch_ms << '-' << std::hex << std::setfill('0')
          << std::setw(8) << bytes(random) << std::setw(8) << bytes(random);
    return value.str();
}
