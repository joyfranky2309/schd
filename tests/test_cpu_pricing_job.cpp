#include <gtest/gtest.h>

#include "cpu_pricing_job.hpp"

TEST(CpuPricingJob, ReportsCleanAndDirtyPrices) {
    CpuPricingJob job(
        "cpu-price",
        100.0,
        0.05,
        5,
        0.05,
        "2024-01-02",
        1);

    const ExecResult result = job.execute();

    EXPECT_EQ(result.status, Job_Status::SUCCESS);
    EXPECT_NE(result.message.find("clean price="), std::string::npos);
    EXPECT_NE(result.message.find("dirty price="), std::string::npos);
    EXPECT_NE(result.message.find("2024-01-02"), std::string::npos);
}
