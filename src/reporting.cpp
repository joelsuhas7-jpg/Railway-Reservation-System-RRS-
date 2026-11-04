#include "reporting.hpp"

#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "authorization.hpp"
#include "fare_config.hpp"

namespace rrs {

ReportingModule::ReportingModule(PersistenceStore store, const AuditLog& audit_log)
    : store_(std::move(store)), audit_log_(audit_log) {}

ReportResult ReportingModule::build(const Session& session, std::string_view timestamp,
                                    const ReportFilter& filter) const {
    ReportResult result;
    try {
        const auto authorization = AuthorizationModule::require_administrator(
            session, "BOOKING_REPORT", timestamp, audit_log_);
        if (!authorization) {
            result.outcome = Outcome::refusal(authorization.message);
            return result;
        }
        if (filter.from_date && !is_valid_fare_date(*filter.from_date)) {
            result.outcome = Outcome::validation_failure("from date", "enter a valid YYYY-MM-DD date");
            return result;
        }
        if (filter.to_date && !is_valid_fare_date(*filter.to_date)) {
            result.outcome = Outcome::validation_failure("to date", "enter a valid YYYY-MM-DD date");
            return result;
        }
        if (filter.from_date && filter.to_date && *filter.from_date > *filter.to_date) {
            result.outcome =
                Outcome::validation_failure("to date", "must not be before the from date");
            return result;
        }

        std::map<std::string, CancellationRecord> cancellations;
        for (auto& record : read_cancellations(store_)) {
            auto pnr = record.pnr;
            cancellations.emplace(std::move(pnr), std::move(record));
        }
        auto& report = result.report;
        for (auto& booking : read_bookings(store_)) {
            // ISO dates compare correctly as text.
            if ((filter.train_number && booking.train_number != *filter.train_number) ||
                (filter.from_date && booking.journey_date < *filter.from_date) ||
                (filter.to_date && booking.journey_date > *filter.to_date)) {
                continue;
            }
            ReportRow row;
            const auto found = cancellations.find(booking.pnr);
            if (found != cancellations.end()) {
                row.cancellation = found->second;
                ++report.cancellation_count;
                report.refund_payable_paise += found->second.refund_paise;
                if (found->second.status() == RefundStatus::Pending) {
                    report.refund_outstanding_paise += found->second.refund_paise;
                    report.outstanding.push_back(found->second);
                }
            }
            ++report.booking_count;
            report.fare_collected_paise += booking.total_fare_paise;
            row.booking = std::move(booking);
            report.rows.push_back(std::move(row));
        }

        const auto scope = (filter.train_number ? std::to_string(*filter.train_number) : "*") +
                           ":" + filter.from_date.value_or("*") + ":" + filter.to_date.value_or("*");
        audit_log_.append({std::string(timestamp), session.user_id, "ADMIN", "REPORT_VIEWED", scope});
        result.outcome = Outcome::success(std::to_string(report.booking_count) + " bookings, " +
                                          std::to_string(report.cancellation_count) +
                                          " cancellations");
        return result;
    } catch (const std::exception& error) {
        result.outcome = Outcome::system_failure(error.what());
        result.report = {};
        return result;
    }
}

}  // namespace rrs
