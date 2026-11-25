#include "cpu_pricing_job.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

CpuPricingJob::CpuPricingJob(std::string id, double face_value, double coupon_rate,
                             int maturity_years, double yield, std::string settlement_date,
                             int max_attempts)
    : Job(std::move(id), max_attempts), face_value_(face_value), coupon_rate_(coupon_rate),
      maturity_years_(maturity_years), yield_(yield), settlement_date_(std::move(settlement_date)) {
}

ExecResult CpuPricingJob::execute() {
    constexpr std::uint32_t sample_count = 20000;
    double price_sum = 0.0;

    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        double price = 0.0;
        for (int year = 1; year <= maturity_years_; ++year) {
            const double coupon = face_value_ * coupon_rate_;
            const double cashflow = year == maturity_years_ ? coupon + face_value_ : coupon;
            price += cashflow / std::pow(1.0 + yield_, static_cast<double>(year));
        }
        price_sum += price;
    }

    const double price = price_sum / sample_count;

    std::ostringstream message;
    message << std::setprecision(10) << "price=" << price << "; samples=" << sample_count
            << "; settlement_date=" << settlement_date_;

    return ExecResult{Job_Status::SUCCESS, 0, message.str()};
}
