#include "quantlib_bond_job.hpp"

#include <exception>
#include <iomanip>
#include <mutex>
#include <ql/errors.hpp>
#include <ql/instruments/bonds/fixedratebond.hpp>
#include <ql/pricingengines/bond/discountingbondengine.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/thirty360.hpp>
#include <ql/time/schedule.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
// QuantLib's evaluation date is process-global, so pricing must be serialized.
std::mutex evaluation_date_mutex;

class EvaluationDateGuard {
public:
    EvaluationDateGuard() : previous_date_(QuantLib::Settings::instance().evaluationDate()) {}

    ~EvaluationDateGuard() { QuantLib::Settings::instance().evaluationDate() = previous_date_; }

private:
    QuantLib::Date previous_date_;
};
} // namespace

QuantLibBondJob::QuantLibBondJob(std::string id, double face_value, double coupon_rate,
                                 int maturity_years, double yield, std::string issue_date,
                                 std::string settlement_date, int max_attempts)
    : Job(std::move(id), max_attempts), face_value_(face_value), coupon_rate_(coupon_rate),
      maturity_years_(maturity_years), yield_(yield), issue_date_(std::move(issue_date)),
      settlement_date_(std::move(settlement_date)) {}

ExecResult QuantLibBondJob::execute() {
    try {
        std::lock_guard<std::mutex> lock(evaluation_date_mutex);
        EvaluationDateGuard restore_evaluation_date;

        const QuantLib::Date settlement_date(
            std::stoi(settlement_date_.substr(8, 2)),
            static_cast<QuantLib::Month>(std::stoi(settlement_date_.substr(5, 2))),
            std::stoi(settlement_date_.substr(0, 4)));
        const QuantLib::Date issue_date(
            std::stoi(issue_date_.substr(8, 2)),
            static_cast<QuantLib::Month>(std::stoi(issue_date_.substr(5, 2))),
            std::stoi(issue_date_.substr(0, 4)));
        QuantLib::Settings::instance().evaluationDate() = settlement_date;

        const QuantLib::Date maturity_date =
            issue_date + QuantLib::Period(maturity_years_, QuantLib::Years);
        const QuantLib::Schedule schedule(
            issue_date, maturity_date, QuantLib::Period(QuantLib::Annual), QuantLib::NullCalendar(),
            QuantLib::Following, QuantLib::Following, QuantLib::DateGeneration::Backward, false);
        const QuantLib::DayCounter day_counter =
            QuantLib::Thirty360(QuantLib::Thirty360::BondBasis);
        QuantLib::FixedRateBond bond(0, face_value_, schedule,
                                     std::vector<QuantLib::Rate>{coupon_rate_}, day_counter,
                                     QuantLib::Following, 100.0, issue_date);
        const auto yield_curve = QuantLib::ext::make_shared<QuantLib::FlatForward>(
            settlement_date, yield_, day_counter, QuantLib::Compounded, QuantLib::Annual);
        const QuantLib::Handle<QuantLib::YieldTermStructure> yield_curve_handle(yield_curve);
        bond.setPricingEngine(
            QuantLib::ext::make_shared<QuantLib::DiscountingBondEngine>(yield_curve_handle));
        const QuantLib::Date reported_evaluation_date(
            QuantLib::Settings::instance().evaluationDate());
        std::ostringstream evaluation_date_message;
        evaluation_date_message << std::setfill('0') << std::setw(4)
                                << reported_evaluation_date.year() << '-' << std::setw(2)
                                << static_cast<unsigned int>(reported_evaluation_date.month())
                                << '-' << std::setw(2) << reported_evaluation_date.dayOfMonth();

        std::ostringstream message;
        message << std::setprecision(10) << "npv=" << bond.NPV()
                << "; clean_price=" << bond.cleanPrice() << "; dirty_price=" << bond.dirtyPrice()
                << "; accrued=" << bond.accruedAmount() << "; settlement_date=" << settlement_date_
                << "; evaluation_date=" << evaluation_date_message.str();
        return ExecResult{Job_Status::SUCCESS, 0, message.str()};
    } catch (const QuantLib::Error& error) {
        return ExecResult{Job_Status::FAILED, -1, error.what()};
    } catch (const std::exception& error) {
        return ExecResult{Job_Status::FAILED, -1, error.what()};
    }
}
