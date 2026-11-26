#include <filesystem>
#include <gtest/gtest.h>
#include <unistd.h>

#include "history_store.hpp"

#ifdef SCHD_WITH_SQLITE
namespace {
std::filesystem::path temporary_db(const char* suffix) {
    return std::filesystem::temp_directory_path() /
           (std::string("schd-history-") + std::to_string(::getpid()) + "-" + suffix + ".db");
}

RunRecord record(const std::string& job_id, int attempt, Job_Status status,
                 int exit_code, std::string message) {
    return RunRecord{job_id, attempt, status, std::nullopt, std::nullopt,
                     std::nullopt, exit_code, std::move(message)};
}
} // namespace

TEST(HistoryStore, RoundTripRetriesAndSkippedRecord) {
    const auto path = temporary_db("roundtrip");
    std::error_code error;
    std::filesystem::remove(path, error);

    {
        HistoryStore store(path.string());
        const std::string run_id = generate_run_id();
        std::vector<RunRecord> records{
            record("retry-job", 1, Job_Status::FAILED, 7, "retry one"),
            record("retry-job", 2, Job_Status::SUCCESS, 0, "retry success"),
            record("skipped-job", 0, Job_Status::SKIPPED, 0, "dependency failed")};
        store.save_run(run_id, records);

        const auto rows = store.history(nullptr, 100);
        ASSERT_EQ(rows.size(), 3U);
        EXPECT_EQ(rows[0].job_id, "skipped-job");
        EXPECT_EQ(rows[0].attempt, 0);
        EXPECT_EQ(rows[0].status, "SKIPPED");
        EXPECT_EQ(rows[1].job_id, "retry-job");
        EXPECT_EQ(rows[1].attempt, 2);
        EXPECT_EQ(rows[1].status, "SUCCESS");
        EXPECT_EQ(rows[2].attempt, 1);
        EXPECT_EQ(rows[2].status, "FAILED");
    }

    std::filesystem::remove(path, error);
}

TEST(HistoryStore, FiltersByJobAndUsesNewestRows) {
    const auto path = temporary_db("filter");
    std::error_code error;
    std::filesystem::remove(path, error);

    {
        HistoryStore store(path.string());
        store.save_run("run-one", {record("alpha", 1, Job_Status::SUCCESS, 0, "first")});
        store.save_run("run-two", {record("beta", 1, Job_Status::FAILED, 9, "second")});
        store.save_run("run-three", {record("alpha", 1, Job_Status::SUCCESS, 0, "third")});

        const std::string job = "alpha";
        const auto rows = store.history(&job, 10);
        ASSERT_EQ(rows.size(), 2U);
        EXPECT_EQ(rows[0].run_id, "run-three");
        EXPECT_EQ(rows[1].run_id, "run-one");
    }

    std::filesystem::remove(path, error);
}

TEST(HistoryStore, GeneratedRunIdsDiffer) {
    EXPECT_NE(generate_run_id(), generate_run_id());
}

TEST(HistoryStore, ReadonlyMissingDatabaseFailsWithoutCreatingFile) {
    const auto path = temporary_db("missing-readonly");
    std::error_code error;
    std::filesystem::remove(path, error);

    try {
        static_cast<void>(HistoryStore::open_readonly(path.string()));
        FAIL() << "missing read-only database did not throw";
    } catch (const std::runtime_error& thrown) {
        EXPECT_EQ(thrown.what(), std::string("no such database: ") + path.string());
    }

    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(HistoryStore, ReadonlyOpenDoesNotCreateOrModifyDatabase) {
    const auto path = temporary_db("readonly-existing");
    std::error_code error;
    std::filesystem::remove(path, error);

    {
        HistoryStore creator(path.string());
        creator.save_run("run-one",
                         {record("alpha", 1, Job_Status::SUCCESS, 0, "created")});
    }

    {
        const HistoryStore store = HistoryStore::open_readonly(path.string());
        const auto rows = store.history(nullptr, 100);
        ASSERT_EQ(rows.size(), 1U);
        EXPECT_EQ(rows[0].run_id, "run-one");
    }

    std::filesystem::remove(path, error);
}

TEST(HistoryStore, BadPathThrowsSqliteError) {
    EXPECT_THROW(HistoryStore("/definitely/not/a/directory/history.db"),
                 std::runtime_error);
}
#endif
