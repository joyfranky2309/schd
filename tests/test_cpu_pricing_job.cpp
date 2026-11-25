#include <gtest/gtest.h>
#include <string>

#include "cpu_pricing_job.hpp"

namespace {
double reported_price(const std::string& message) {
    const std::string prefix = "price=";
    const std::size_t start = message.find(prefix);
    EXPECT_NE(start, std::string::npos);
    if (start == std::string::npos) {
        return 0.0;
    }
    const std::size_t value_start = start + prefix.size();
    const std::size_t end = message.find(';', value_start);
    return std::stod(message.substr(value_start, end - value_start));
}
} // namespace

TEST(CpuPricingJob, ParCouponPricesNearFaceValue) {
    CpuPricingJob job("cpu-price", 100.0, 0.05, 5, 0.05, "2024-01-02", 1);

    const ExecResult result = job.execute();

    ASSERT_EQ(result.status, Job_Status::SUCCESS);
    EXPECT_NEAR(reported_price(result.message), 100.0, 0.5);
    EXPECT_NE(result.message.find("price="), std::string::npos);
    EXPECT_NE(result.message.find("samples="), std::string::npos);
    EXPECT_NE(result.message.find("settlement_date=2024-01-02"), std::string::npos);
    EXPECT_EQ(result.message.find("clean price="), std::string::npos);
    EXPECT_EQ(result.message.find("dirty price="), std::string::npos);
}

TEST(CpuPricingJob, LowerYieldProducesHigherPrice) {
    CpuPricingJob lower_yield("lower-yield", 100.0, 0.05, 5, 0.04, "2024-01-02", 1);
    CpuPricingJob higher_yield("higher-yield", 100.0, 0.05, 5, 0.05, "2024-01-02", 1);

    EXPECT_GT(reported_price(lower_yield.execute().message),
              reported_price(higher_yield.execute().message));
}

TEST(CpuPricingJob, RepeatedExecutionsHaveIdenticalMessages) {
    CpuPricingJob job("cpu-price", 100.0, 0.05, 5, 0.04, "2024-01-02", 1);

    const ExecResult first = job.execute();
    const ExecResult second = job.execute();

    EXPECT_EQ(first.message, second.message);
}
