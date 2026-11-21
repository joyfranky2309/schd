#include <gtest/gtest.h>

#ifdef SCHD_WITH_QUANTLIB

#include <memory>
#include <string>
#include <vector>

#include <ql/settings.hpp>
#include <ql/time/date.hpp>

#include "quantlib_bond_job.hpp"
#include "scheduler.hpp"

namespace {
double reported_value(const std::string& message, const std::string& field) {
    const std::string prefix = field + "=";
    const std::size_t start = message.find(prefix);
    EXPECT_NE(start, std::string::npos);
    if (start == std::string::npos) {
        return 0.0;
    }
    const std::size_t value_start = start + prefix.size();
    const std::size_t end = message.find(';', value_start);
    return std::stod(message.substr(value_start, end - value_start));
}
}

TEST(QuantLibBondJob, ParCouponPricesNearPar) {
    QuantLibBondJob job(
        "par-bond", 1000.0, 0.05, 5, 0.05, "2024-01-02", 1);

    const ExecResult result = job.execute();

    ASSERT_EQ(result.status, Job_Status::SUCCESS) << result.message;
    EXPECT_NEAR(reported_value(result.message, "clean_price"), 100.0, 0.5);
    EXPECT_NEAR(reported_value(result.message, "npv"), 1000.0, 5.0);
    EXPECT_NE(result.message.find("npv="), std::string::npos);
    EXPECT_NE(result.message.find("dirty_price="), std::string::npos);
    EXPECT_NE(result.message.find("accrued="), std::string::npos);
    EXPECT_NE(result.message.find("settlement_date=2024-01-02"), std::string::npos);
}

TEST(QuantLibBondJob, HigherYieldProducesLowerPrice) {
    QuantLibBondJob lower_yield(
        "lower-yield", 100.0, 0.05, 5, 0.04, "2024-01-02", 1);
    QuantLibBondJob higher_yield(
        "higher-yield", 100.0, 0.05, 5, 0.05, "2024-01-02", 1);

    const ExecResult lower_result = lower_yield.execute();
    const ExecResult higher_result = higher_yield.execute();

    ASSERT_EQ(lower_result.status, Job_Status::SUCCESS) << lower_result.message;
    ASSERT_EQ(higher_result.status, Job_Status::SUCCESS) << higher_result.message;
    EXPECT_GT(
        reported_value(lower_result.message, "clean_price"),
        reported_value(higher_result.message, "clean_price"));
}

TEST(QuantLibBondJob, RestoresPreviousEvaluationDate) {
    const QuantLib::Date previous_date(15, QuantLib::September, 2011);
    QuantLib::Settings::instance().evaluationDate() = previous_date;
    QuantLibBondJob job(
        "date-restore", 100.0, 0.05, 5, 0.05, "2024-01-02", 1);

    const ExecResult result = job.execute();

    ASSERT_EQ(result.status, Job_Status::SUCCESS) << result.message;
    EXPECT_EQ(
        QuantLib::Date(QuantLib::Settings::instance().evaluationDate()),
        previous_date);
}

TEST(QuantLibBondJob, DifferentSettlementDatesAreSafeAcrossWorkers) {
    std::vector<std::unique_ptr<QuantLibBondJob>> jobs;
    std::vector<std::string> settlement_dates;
    jobs.reserve(8);
    settlement_dates.reserve(8);
    Scheduler scheduler(4);
    for (int day = 2; day < 10; ++day) {
        const std::string id = "bond-" + std::to_string(day);
        const std::string date =
            "2024-01-0" + std::to_string(day);
        jobs.push_back(std::make_unique<QuantLibBondJob>(
            id, 100.0, 0.05, 5, 0.04, date, 1));
        scheduler.add_job(jobs.back().get(), 1, 1);
        settlement_dates.push_back(date);
    }

    scheduler.start();
    scheduler.wait();

    for (std::size_t index = 0; index < jobs.size(); ++index) {
        const auto& job = jobs[index];
        EXPECT_EQ(scheduler.final_status(job->id()), Job_Status::SUCCESS)
            << job->id();
        const auto history = scheduler.history(job->id());
        ASSERT_EQ(history.size(), 1U);
        EXPECT_NE(
            history.front().message.find(
                "settlement_date=" + settlement_dates[index]),
            std::string::npos);
    }
}

#endif
