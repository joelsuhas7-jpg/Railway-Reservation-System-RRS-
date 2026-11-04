#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "authorization.hpp"
#include "booking.hpp"
#include "cancellation_refund.hpp"
#include "class_fare_config.hpp"
#include "fare_calculation.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "report_flow.hpp"
#include "reporting.hpp"
#include "seat_inventory.hpp"
#include "session_context.hpp"
#include "train_catalogue.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-report-flow-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create flow test directory",
                                                        path_, error);
            }
        }
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

constexpr std::string_view kSetupTime = "2026-10-30T10:00:00Z";
const rrs::Session kAdmin{"admin", rrs::Role::Administrator};
const rrs::Session kPassenger{"passenger1", rrs::Role::Passenger};

struct Modules {
    explicit Modules(const std::filesystem::path& directory, const rrs::AuditLog& audit_log)
        : store{directory},
          trains{store, audit_log},
          classes{store, trains, audit_log, [] { return std::string(kSetupTime); }},
          fares{store, trains, classes, audit_log, [] { return std::string(kSetupTime); }},
          inventory{store, classes, audit_log, [] { return std::string(kSetupTime); }},
          calculation{fares},
          booking{store, calculation, inventory, audit_log},
          cancellations{store, trains, inventory, audit_log},
          reporting{store, audit_log} {}

    std::string book(std::uint32_t train, const std::string& date, int passengers) {
        rrs::BookingRequest request;
        request.train_number = train;
        request.class_code = "SL";
        request.journey_date = date;
        for (int i = 0; i < passengers; ++i) {
            request.passengers.push_back({"Passenger " + std::to_string(i + 1), 30,
                                          rrs::Gender::Other});
        }
        request.payment = {rrs::PaymentOutcome::success, "PAY-" + date};
        const auto booked = booking.book(kPassenger, kSetupTime, request);
        REQUIRE(booked.kind == rrs::OutcomeKind::Success);
        return booked.detail;
    }

    rrs::PersistenceStore store;
    rrs::TrainCatalogueModule trains;
    rrs::ClassFareConfigModule classes;
    rrs::FareConfigModule fares;
    rrs::SeatInventoryModule inventory;
    rrs::FareCalculationModule calculation;
    rrs::BookingModule booking;
    rrs::CancellationRefundModule cancellations;
    rrs::ReportingModule reporting;
};

struct Fixture {
    std::string settled;  // 12345 on 2026-11-02, four passengers, S2 refund settled
    std::string pending;  // 12345 on 2026-11-05, two passengers, S1 refund PENDING
};

// Trains 12345 (SL 45000 paise, 06:00) and 22222 (SL 30000 paise, 21:00) with four bookings.
Fixture seed(const rrs::UserStore& users, const rrs::AuditLog& audit_log) {
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));
    Modules modules{users.data_directory(), audit_log};
    REQUIRE(modules.trains
                .add_train(kAdmin, kSetupTime,
                           {12345, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f})
                .kind == rrs::OutcomeKind::Success);
    REQUIRE(modules.trains
                .add_train(kAdmin, kSetupTime,
                           {22222, "Udyan Express", "SBC", "CSMT", "21:00", "23:00", 0x7f})
                .kind == rrs::OutcomeKind::Success);
    for (const std::uint32_t train : {12345U, 22222U}) {
        REQUIRE(modules.classes.add_class(kAdmin, kSetupTime, train, "SL", 72).kind ==
                rrs::OutcomeKind::Success);
    }
    REQUIRE(modules.fares.configure_fare(kAdmin, 12345, "SL", 45000, "2026-10-30").kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.fares.configure_fare(kAdmin, 22222, "SL", 30000, "2026-10-30").kind ==
            rrs::OutcomeKind::Success);

    Fixture fixture;
    fixture.settled = modules.book(12345, "2026-11-02", 4);  // 180000
    fixture.pending = modules.book(12345, "2026-11-05", 2);  // 90000
    modules.book(22222, "2026-11-02", 1);                    // 30000
    modules.book(12345, "2026-11-02", 1);                    // 45000

    REQUIRE(modules.cancellations.cancel(kPassenger, "2026-11-01T10:00:00Z", fixture.settled)
                .kind == rrs::OutcomeKind::Success);  // 20 h before: S2, 90000
    REQUIRE(modules.cancellations
                .settle_refund(kAdmin, kSetupTime, fixture.settled, "DISB-0001")
                .kind == rrs::OutcomeKind::Success);
    REQUIRE(modules.cancellations.cancel(kPassenger, "2026-10-31T04:00:00Z", fixture.pending)
                .kind == rrs::OutcomeKind::Success);  // 98 h before: S1, 81000
    return fixture;
}

// Totals recomputed directly from bookings.dat and cancellations.dat for the same filter.
void check_reconciles(Modules& modules, const rrs::ReportFilter& filter) {
    const auto built = modules.reporting.build(kAdmin, kSetupTime, filter);
    REQUIRE(built.outcome.kind == rrs::OutcomeKind::Success);
    std::map<std::string, rrs::CancellationRecord> cancellations;
    for (const auto& record : rrs::read_cancellations(modules.store)) {
        cancellations.emplace(record.pnr, record);
    }
    std::size_t bookings = 0;
    std::size_t cancelled = 0;
    std::int64_t fare = 0;
    std::int64_t refund = 0;
    std::int64_t outstanding = 0;
    for (const auto& booking : rrs::read_bookings(modules.store)) {
        if ((filter.train_number && booking.train_number != *filter.train_number) ||
            (filter.from_date && booking.journey_date < *filter.from_date) ||
            (filter.to_date && booking.journey_date > *filter.to_date)) {
            continue;
        }
        ++bookings;
        fare += booking.total_fare_paise;
        const auto found = cancellations.find(booking.pnr);
        if (found != cancellations.end()) {
            ++cancelled;
            refund += found->second.refund_paise;
            if (found->second.status() == rrs::RefundStatus::Pending) {
                outstanding += found->second.refund_paise;
            }
        }
    }
    CHECK(built.report.rows.size() == bookings);
    CHECK(built.report.booking_count == bookings);
    CHECK(built.report.cancellation_count == cancelled);
    CHECK(built.report.fare_collected_paise == fare);
    CHECK(built.report.refund_payable_paise == refund);
    CHECK(built.report.refund_outstanding_paise == outstanding);
}

std::string read_audit(const std::filesystem::path& directory) {
    std::ifstream file(directory / "audit.log");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("TC-REPORT-01: totals are exact for each filter", "[integration][M16]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed(users, audit_log);
    Modules modules{dir.path(), audit_log};

    rrs::ReportFilter by_train;
    by_train.train_number = 12345;
    const auto train = modules.reporting.build(kAdmin, kSetupTime, by_train).report;
    CHECK(train.booking_count == 3);
    CHECK(train.cancellation_count == 2);
    CHECK(train.fare_collected_paise == 315000);
    CHECK(train.refund_payable_paise == 171000);
    CHECK(train.refund_outstanding_paise == 81000);
    CHECK(train.outstanding.size() == 1);

    rrs::ReportFilter by_date;
    by_date.from_date = "2026-11-02";
    by_date.to_date = "2026-11-02";
    const auto date = modules.reporting.build(kAdmin, kSetupTime, by_date).report;
    CHECK(date.booking_count == 3);
    CHECK(date.cancellation_count == 1);
    CHECK(date.fare_collected_paise == 255000);
    CHECK(date.refund_payable_paise == 90000);
    CHECK(date.refund_outstanding_paise == 0);

    rrs::ReportFilter both = by_date;
    both.train_number = 12345;
    const auto combined = modules.reporting.build(kAdmin, kSetupTime, both).report;
    CHECK(combined.booking_count == 2);
    CHECK(combined.fare_collected_paise == 225000);

    rrs::ReportFilter none;
    none.train_number = 99999;
    const auto empty = modules.reporting.build(kAdmin, kSetupTime, none).report;
    CHECK(empty.booking_count == 0);
    CHECK(empty.fare_collected_paise == 0);

    rrs::ReportFilter from_only;
    from_only.from_date = "2026-11-03";
    for (const auto& filter : {by_train, by_date, both, none, from_only, rrs::ReportFilter{}}) {
        check_reconciles(modules, filter);
    }
}

TEST_CASE("TC-REPORT-01: a passenger cannot invoke the report", "[integration][M5][M16]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed(users, audit_log);
    Modules modules{dir.path(), audit_log};

    const auto refused = modules.reporting.build(kPassenger, kSetupTime, {});
    CHECK(refused.outcome.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(refused.report.rows.empty());
    CHECK(read_audit(dir.path()).find("passenger1\tPASSENGER\tACCESS_DENIED_BOOKING_REPORT") !=
          std::string::npos);
    CHECK(rrs::AuthorizationModule::authorize(&kPassenger, rrs::MenuAction::BookingReport).kind !=
          rrs::OutcomeKind::Success);
}

TEST_CASE("TC-REPORT-01: malformed and reversed date ranges are refused naming the field",
          "[integration][M16]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed(users, audit_log);
    Modules modules{dir.path(), audit_log};

    rrs::ReportFilter malformed;
    malformed.from_date = "2026-02-30";
    const auto bad = modules.reporting.build(kAdmin, kSetupTime, malformed).outcome;
    CHECK(bad.kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(bad.detail == "from date");

    rrs::ReportFilter reversed;
    reversed.from_date = "2026-11-05";
    reversed.to_date = "2026-11-02";
    const auto backwards = modules.reporting.build(kAdmin, kSetupTime, reversed).outcome;
    CHECK(backwards.kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(backwards.detail == "to date");
}

TEST_CASE("TC-REPORT-01: the administrator console shows totals and settles a pending refund",
          "[integration][M15][M16][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    const auto fixture = seed(users, audit_log);

    // Administrator menu: 5 Booking and cancellation report, 8 Logout.
    std::istringstream in("admin\nAdminPassword1\n5\n12345\n\n\n" + fixture.pending +
                          "\nDISB-0002\n\n8\n");
    std::ostringstream out;
    std::ostringstream err;
    const rrs::AppContext context{in, out, err, users, audit_log,
                                  [] { return std::string(kSetupTime); }, {}};
    rrs::MenuRouter router;
    rrs::register_account_flows(router);
    rrs::register_report_flows(router);
    rrs::login_and_run_session(context, router);
    const auto text = out.str();

    CHECK(text.find(fixture.settled + " | 12345 | SL | 2026-11-02 | 4 | Rs 1800.00 | CANCELLED | "
                                      "Rs 900.00 S2 SETTLED") != std::string::npos);
    CHECK(text.find("Bookings: 3 | Cancellations: 2 | Fare collected: Rs 3150.00 | "
                    "Refund payable: Rs 1710.00") != std::string::npos);
    CHECK(text.find("Outstanding refunds: 1 totalling Rs 810.00") != std::string::npos);
    CHECK(text.find("OK: refund settled (ref: " + fixture.pending + ")") != std::string::npos);
    CHECK(text.find("OK: report closed; 1 refund settled") != std::string::npos);
    CHECK(err.str().empty());

    Modules modules{dir.path(), audit_log};
    CHECK(modules.cancellations.find_cancellation(fixture.pending)->status() ==
          rrs::RefundStatus::Settled);
    CHECK(read_audit(dir.path()).find("admin\tADMIN\tREPORT_VIEWED\t12345:*:*") !=
          std::string::npos);
}
