#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "job_loader.hpp"

namespace {
class TemporaryJsonFile {
public:
    explicit TemporaryJsonFile(const std::string& contents) {
        static std::atomic<unsigned int> next_id{0};
        path_ = std::filesystem::temp_directory_path() /
            ("schd-job-file-" + std::to_string(::getpid()) + "-" +
             std::to_string(next_id.fetch_add(1)) + ".json");
        std::ofstream output(path_);
        if (!output) {
            throw std::runtime_error("could not create temporary job file");
        }
        output << contents;
        if (!output) {
            throw std::runtime_error("could not write temporary job file");
        }
    }

    ~TemporaryJsonFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};
}

TEST(JobLoader, LoadsRegistrationsDefaultsAndIgnoresUnknownFields) {
    TemporaryJsonFile file(R"json({
        "workers": 4,
        "ignored": true,
        "jobs": [
            {
                "id": "extract",
                "type": "shell",
                "command": "echo extract",
                "category": "EOD",
                "priority": 5,
                "max_attempts": 2
            },
            {
                "id": "load",
                "type": "shell",
                "command": "echo load",
                "category": "ADHOC",
                "depends_on": ["extract"],
                "unknown_job_field": 17
            }
        ]
    })json");

    const auto loaded = load_job_file(file.path());

    ASSERT_EQ(loaded.jobs.size(), 2U);
    ASSERT_EQ(loaded.registrations.size(), 2U);
    EXPECT_EQ(loaded.workers, 4U);
    EXPECT_EQ(loaded.jobs[0]->id(), "extract");
    EXPECT_EQ(loaded.jobs[0]->max_attempts(), 2);
    EXPECT_EQ(loaded.registrations[0].job, loaded.jobs[0].get());
    EXPECT_EQ(loaded.registrations[0].category, 2);
    EXPECT_EQ(loaded.registrations[0].priority, 5);
    EXPECT_TRUE(loaded.registrations[0].dependencies.empty());
    EXPECT_EQ(loaded.jobs[1]->max_attempts(), 1);
    EXPECT_EQ(loaded.registrations[1].category, 1);
    EXPECT_EQ(loaded.registrations[1].priority, 1);
    EXPECT_EQ(
        loaded.registrations[1].dependencies,
        (std::vector<std::string>{"extract"}));
}

TEST(JobLoader, DefaultsWorkersToSeven) {
    TemporaryJsonFile file(R"json({
        "jobs": [
            {"id": "one", "type": "shell", "command": "true", "category": "ADHOC"}
        ]
    })json");

    EXPECT_EQ(load_job_file(file.path()).workers, 7U);
}

TEST(JobLoader, RejectsInvalidJobDefinitionsAndNamesTheJob) {
    const std::vector<std::pair<std::string, std::string>> invalid_files{
        {
            R"json({"jobs":[
                {"id":"duplicate","type":"shell","command":"true","category":"EOD"},
                {"id":"duplicate","type":"shell","command":"true","category":"EOD"}
            ]})json",
            "duplicate"},
        {
            R"json({"jobs":[
                {"id":"unknown-dependency","type":"shell","command":"true",
                 "category":"EOD","depends_on":["missing"]}
            ]})json",
            "unknown-dependency"},
        {
            R"json({"jobs":[
                {"id":"unknown-type","type":"python","command":"true","category":"EOD"}
            ]})json",
            "unknown-type"},
        {
            R"json({"jobs":[
                {"id":"bad-category","type":"shell","command":"true","category":"other"}
            ]})json",
            "bad-category"},
        {
            R"json({"jobs":[
                {"id":"bad-command","type":"shell","category":"EOD"}
            ]})json",
            "bad-command"},
        {
            R"json({"jobs":[
                {"id":"bad-attempts","type":"shell","command":"true",
                 "category":"EOD","max_attempts":0}
            ]})json",
            "bad-attempts"}};

    for (const auto& [contents, offending_id] : invalid_files) {
        TemporaryJsonFile file(contents);
        try {
            static_cast<void>(load_job_file(file.path()));
            FAIL() << "Expected invalid job file for " << offending_id;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find(offending_id), std::string::npos);
        }
    }
}
