#include "cpu_pricing_job.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <sstream>
#include <string>

CpuPricingJob::CpuPricingJob(
    std::string id,
    double face_value,
    double coupon_rate,
    int maturity_years,
    double yield,
    std::string settlement_date,
    int max_attempts)
    : Job(std::move(id), max_attempts),
      face_value_(face_value),
      coupon_rate_(coupon_rate),
      maturity_years_(maturity_years),
      yield_(yield),
      settlement_date_(std::move(settlement_date)) {}

ExecResult CpuPricingJob::execute() {
    constexpr std::uint32_t sample_count = 20000;
    std::uint32_t state = 0x5u;
    double dirty_price_sum = 0.0;

    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        double present_value = 0.0;
        for (int year = 1; year <= maturity_years_; ++year) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const double random_unit =
                static_cast<double>(state) /
                static_cast<double>(std::numeric_limits<std::uint32_t>::max());
            const double simulated_yield = yield_ + (random_unit - 0.5) * 0.004;
            const double coupon = face_value_ * coupon_rate_;
            const double cashflow =
                year == maturity_years_ ? coupon + face_value_ : coupon;
            present_value +=
                cashflow / std::pow(1.0 + simulated_yield, static_cast<double>(year));
        }
        dirty_price_sum += present_value;
    }

    const double dirty_price = dirty_price_sum / sample_count;
    const double clean_price = dirty_price;

    std::ostringstream message;
    message << std::setprecision(10)
            << "clean price=" << clean_price
            << "; dirty price=" << dirty_price
            << "; settlement_date=" << settlement_date_
            << "; deterministic_monte_carlo_samples=" << sample_count;

    return ExecResult{Job_Status::SUCCESS, 0, message.str()};
}
