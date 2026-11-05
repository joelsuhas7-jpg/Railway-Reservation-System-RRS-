#include "cancel_flow.hpp"

#include <cctype>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

#include "booking_flow.hpp"
#include "cancellation_refund.hpp"
#include "class_fare_config.hpp"
#include "console_input.hpp"
#include "persistence.hpp"
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

void render_cancellation(std::ostream& out, const CancellationRecord& record) {
    out << "Cancelled at " << record.cancelled_at << " | slab " << refund_slab_name(record.slab)
        << " | refund " << rupees(record.refund_paise) << " | "
        << refund_status_name(record.status());
    if (!record.disbursement_reference.empty()) {
        out << " | disbursement " << record.disbursement_reference;
    }
    out << '\n';
}

// The M6 to M8 and M15 modules over one data directory, opened for a single cancellation.
class CancellationServices {
public:
    explicit CancellationServices(const AppContext& context)
        : store_{context.users.data_directory()},
          trains_{store_, context.audit_log},
          classes_{store_, trains_, context.audit_log, [&context] { return context.now_timestamp(); }},
          inventory_{store_, classes_, context.audit_log,
                     [&context] { return context.now_timestamp(); }},
          cancellations_{store_, trains_, inventory_, context.audit_log} {}

    CancellationRefundModule& cancellations() { return cancellations_; }

private:
    PersistenceStore store_;
    TrainCatalogueModule trains_;
    ClassFareConfigModule classes_;
    SeatInventoryModule inventory_;
    CancellationRefundModule cancellations_;
};

}  // namespace

Outcome cancel_ticket_from_console(const AppContext& context, const Session& session,
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
                         : Outcome::refusal("cancellation stopped; booking unchanged");
    };

    std::string pnr;
    if (!read_input("PNR", pnr)) {
        return read_failure();
    }
    pnr = to_upper(pnr);

    try {
        CancellationServices services{context};
        auto& cancellations = services.cancellations();

        // M2 to M5: ownership, status and slab, then the ticket with its refund.
        const auto now = context.now_timestamp();
        const auto quote = cancellations.quote(session, now, pnr);
        if (quote.booking) {
            render_ticket(context.out, *quote.booking);
        }
        if (quote.existing) {
            render_cancellation(context.out, *quote.existing);  // B3
        }
        if (quote.outcome.kind != OutcomeKind::Success) {
            return quote.outcome;
        }
        context.out << "Departure: " << quote.departure << " | slab " << refund_slab_name(quote.slab)
                    << " | refund " << rupees(quote.refund_paise) << " ("
                    << refund_percentage(quote.slab) << " per cent)\n";

        // M6: declining leaves the booking unchanged (B6).
        std::string answer;
        for (;;) {
            if (!read_input("Cancel this booking? (y/n)", answer)) {
                return read_failure();
            }
            answer = to_upper(answer);
            if (answer == "Y" || answer == "N") {
                break;
            }
            report_outcome(context.out, context.err,
                           Outcome::validation_failure("confirmation", "enter y or n"));
        }
        if (answer == "N") {
            return Outcome::refusal("cancellation declined; booking unchanged");
        }

        // M7 to M9.
        const auto cancelled = cancellations.cancel(session, context.now_timestamp(), pnr);
        if (cancelled.kind != OutcomeKind::Success) {
            return cancelled;
        }
        report_outcome(context.out, context.err, cancelled);
        const auto record = cancellations.find_cancellation(pnr);
        const auto refund = rupees(record ? record->refund_paise : quote.refund_paise);

        // M10: the disbursement reference, or blank to leave the refund PENDING (B8).
        for (;;) {
            std::string reference;
            if (!read_input("Disbursement reference (blank to settle later)", reference) ||
                reference.empty()) {
                return Outcome::success("refund " + refund + " is PENDING settlement", pnr);
            }
            const auto settled =
                cancellations.settle_refund(session, context.now_timestamp(), pnr, reference);
            if (settled.kind != OutcomeKind::ValidationFailure) {
                return settled.kind == OutcomeKind::Success
                           ? Outcome::success("refund " + refund + " SETTLED", pnr)
                           : settled;
            }
            report_outcome(context.out, context.err, settled);
        }
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

void register_cancel_flows(MenuRouter& router) {
    router.add(MenuAction::CancelTicket, cancel_ticket_from_console);
}

}  // namespace rrs
