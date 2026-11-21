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

void expect_invalid_cpu_pricing_params(const std::string& params) {
    TemporaryJsonFile file(
        R"json({"jobs":[{"id":"pricing-invalid","type":"cpu_pricing","category":"EOD","params":)json" +
        params + "}]}");
    try {
        static_cast<void>(load_job_file(file.path()));
        FAIL() << "Expected invalid cpu_pricing params";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(
            std::string(error.what()).find("pricing-invalid"),
            std::string::npos);
    }
}
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

TEST(JobLoader, RejectsNonPositiveCpuPricingFaceValueAndNamesTheJob) {
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":0,"coupon_rate":0.05,"maturity_years":5,"yield":0.05})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":-1,"coupon_rate":0.05,"maturity_years":5,"yield":0.05})json");
}

TEST(JobLoader, RejectsCpuPricingCouponOutsideUnitIntervalAndNamesTheJob) {
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":-0.01,"maturity_years":5,"yield":0.05})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":1.01,"maturity_years":5,"yield":0.05})json");
}

TEST(JobLoader, RejectsCpuPricingYieldOutsideUnitIntervalAndNamesTheJob) {
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":5,"yield":-0.01})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":5,"yield":1.01})json");
}

TEST(JobLoader, RejectsCpuPricingMaturityOutsideIntegerRangeAndNamesTheJob) {
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":0,"yield":0.05})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":101,"yield":0.05})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":1.5,"yield":0.05})json");
}

TEST(JobLoader, RejectsInvalidCpuPricingCalendarDateAndNamesTheJob) {
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":5,"yield":0.05,"settlement_date":"2024-02-30"})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":5,"yield":0.05,"settlement_date":"not-a-date"})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":5,"yield":0.05,"settlement_date":"2024-2-03"})json");
    expect_invalid_cpu_pricing_params(
        R"json({"face_value":100,"coupon_rate":0.05,"maturity_years":5,"yield":0.05,"settlement_date":null})json");
}

TEST(JobLoader, CpuPricingDefaultsSettlementDate) {
    TemporaryJsonFile file(R"json({
        "jobs": [{
            "id": "pricing-default-date",
            "type": "cpu_pricing",
            "category": "EOD",
            "params": {
                "face_value": 100,
                "coupon_rate": 0.05,
                "maturity_years": 5,
                "yield": 0.05
            }
        }]
    })json");

    const auto loaded = load_job_file(file.path());
    const auto result = loaded.jobs.front()->execute();

    EXPECT_NE(result.message.find("settlement_date=2024-01-02"), std::string::npos);
}
