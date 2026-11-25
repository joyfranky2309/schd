#pragma once

#include <string>

#include "job.hpp"

class CpuPricingJob : public Job {
public:
    CpuPricingJob(std::string id, double face_value, double coupon_rate, int maturity_years,
                  double yield, std::string settlement_date, int max_attempts);

    ExecResult execute() override;

private:
    double face_value_;
    double coupon_rate_;
    int maturity_years_;
    double yield_;
    std::string settlement_date_;
};
