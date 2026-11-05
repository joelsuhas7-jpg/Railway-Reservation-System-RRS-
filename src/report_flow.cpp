#include "report_flow.hpp"

#include <cctype>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

#include "booking.hpp"
#include "cancellation_refund.hpp"
#include "class_fare_config.hpp"
#include "console_input.hpp"
#include "persistence.hpp"
#include "reporting.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"

namespace rrs {
namespace {

std::string to_upper(std::string text) {
    for (auto& character : text) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string rupees(std::int64_t paise) {
    std::ostringstream text;
    text << "Rs " << paise / 100 << '.' << std::setw(2) << std::setfill('0') << paise % 100;
    return text.str();
}

void render_report(std::ostream& out, const ReservationReport& report) {
    out << "PNR | Train | Class | Journey date | Passengers | Fare | Status | Refund\n";
    for (const auto& row : report.rows) {
        const auto& booking = row.booking;
        out << booking.pnr << " | " << booking.train_number << " | " << booking.class_code
            << " | " << booking.journey_date << " | " << booking.passengers.size() << " | "
            << rupees(booking.total_fare_paise) << " | " << booking_status_name(booking.status)
            << " | ";
        if (row.cancellation) {
            out << rupees(row.cancellation->refund_paise) << ' '
                << refund_slab_name(row.cancellation->slab) << ' '
                << refund_status_name(row.cancellation->status());
        } else {
            out << '-';
        }
        out << '\n';
    }
    out << "Bookings: " << report.booking_count << " | Cancellations: "
        << report.cancellation_count << " | Fare collected: "
        << rupees(report.fare_collected_paise) << " | Refund payable: "
        << rupees(report.refund_payable_paise) << '\n';
    out << "Outstanding refunds: " << report.outstanding.size() << " totalling "
        << rupees(report.refund_outstanding_paise) << '\n';
    for (const auto& record : report.outstanding) {
        out << "  " << record.pnr << " | " << rupees(record.refund_paise) << " | cancelled "
            << record.cancelled_at << '\n';
    }
}

}  // namespace

Outcome booking_report_from_console(const AppContext& context, const Session& session,
                                    const SessionActivityCheck& session_activity) {
    const auto expired = [&session_activity] {
        return session_activity && !session_activity(false);
    };
    const auto read_input = [&](const char* label, std::string& value) {
        if (expired() || !read_field(context.in, context.out, label, value)) {
            return false;
        }
        return !session_activity || session_activity(true);
    };
    const auto read_failure = [&] {
        return expired() ? Outcome::refusal(std::string(kSessionExpiredMessage))
                         : Outcome::refusal("report cancelled");
    };

    std::string train_text;
    std::string from_date;
    std::string to_date;
    if (!read_input("Train number (blank for all)", train_text) ||
        !read_input("From journey date (YYYY-MM-DD, blank for none)", from_date) ||
        !read_input("To journey date (YYYY-MM-DD, blank for none)", to_date)) {
        return read_failure();
    }
    ReportFilter filter;
    if (!train_text.empty()) {
        std::uint32_t train_number = 0;
        if (!parse_train_number(train_text, train_number)) {
            return Outcome::validation_failure("train number", "enter a positive whole number");
        }
        filter.train_number = train_number;
    }
    if (!from_date.empty()) {
        filter.from_date = from_date;
    }
    if (!to_date.empty()) {
        filter.to_date = to_date;
    }

    try {
        const PersistenceStore store{context.users.data_directory()};
        const auto built =
            ReportingModule{store, context.audit_log}.build(session, context.now_timestamp(), filter);
        if (built.outcome.kind != OutcomeKind::Success) {
            return built.outcome;
        }
        render_report(context.out, built.report);
        if (built.report.outstanding.empty()) {
            return built.outcome;
        }
        report_outcome(context.out, context.err, built.outcome);

        // UC-02 M10 for refunds left PENDING (B8).
        const TrainCatalogueModule trains{store, context.audit_log};
        const auto timestamp = [&context] { return context.now_timestamp(); };
        const ClassFareConfigModule classes{store, trains, context.audit_log, timestamp};
        SeatInventoryModule inventory{store, classes, context.audit_log, timestamp};
        CancellationRefundModule cancellations{store, trains, inventory, context.audit_log};
        int settled = 0;
        for (;;) {
            std::string pnr;
            if (!read_input("Settle refund for PNR (blank to finish)", pnr) || pnr.empty()) {
                break;
            }
            std::string reference;
            if (!read_input("Disbursement reference", reference)) {
                break;
            }
            const auto outcome = cancellations.settle_refund(session, context.now_timestamp(),
                                                             to_upper(pnr), reference);
            if (outcome.kind == OutcomeKind::Success) {
                ++settled;
            }
            report_outcome(context.out, context.err, outcome);
        }
        if (expired()) {
            return Outcome::refusal(std::string(kSessionExpiredMessage));
        }
        return Outcome::success("report closed; " + std::to_string(settled) +
                                (settled == 1 ? " refund settled" : " refunds settled"));
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

void register_report_flows(MenuRouter& router) {
    router.add(MenuAction::BookingReport, booking_report_from_console);
}

}  // namespace rrs
