#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "booking.hpp"
#include "cancel_flow.hpp"
#include "cancellation_refund.hpp"
#include "class_fare_config.hpp"
#include "fare_calculation.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
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
                    ("rrs-cancel-flow-" + std::to_string(nonce) + "-" +
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
constexpr std::string_view kJourneyDate = "2026-11-02";  // train 12345 departs 06:00
// Clock fixture (Test Plan section 6): instants relative to the 2026-11-02 06:00 departure.
constexpr std::string_view k50HoursBefore = "2026-10-31T04:00:00Z";  // S1
constexpr std::string_view k20HoursBefore = "2026-11-01T10:00:00Z";  // S2
constexpr std::string_view k3HoursBefore = "2026-11-02T03:00:00Z";   // S4

// The M6 to M15 modules over the test directory, for checks made outside the console.
struct Modules {
    explicit Modules(const std::filesystem::path& directory, const rrs::AuditLog& audit_log)
        : store{directory},
          trains{store, audit_log},
          classes{store, trains, audit_log, [] { return std::string(kSetupTime); }},
          fares{store, trains, classes, audit_log, [] { return std::string(kSetupTime); }},
          inventory{store, classes, audit_log, [] { return std::string(kSetupTime); }},
          calculation{fares},
          booking{store, calculation, inventory, audit_log},
          cancellations{store, trains, inventory, audit_log} {}

    std::uint32_t free_sl() { return inventory.get_inventory(12345, "SL", kJourneyDate).free_seats; }

    rrs::PersistenceStore store;
    rrs::TrainCatalogueModule trains;
    rrs::ClassFareConfigModule classes;
    rrs::FareConfigModule fares;
    rrs::SeatInventoryModule inventory;
    rrs::FareCalculationModule calculation;
    rrs::BookingModule booking;
    rrs::CancellationRefundModule cancellations;
};

// Known-answer fixture: train 12345 with SL(72) at 45000 paise and two passengers.
void seed_catalogue(const rrs::UserStore& users, const rrs::AuditLog& audit_log) {
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger2", "Pass2Secure", "p2@example.test"));
    Modules modules{users.data_directory(), audit_log};
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains
                .add_train(admin, kSetupTime,
                           {12345, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f})
                .kind == rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kSetupTime, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-30").kind ==
            rrs::OutcomeKind::Success);
}

// A confirmed booking for passenger1; four passengers make the 180000-paise known answer.
std::string book(const rrs::UserStore& users, const rrs::AuditLog& audit_log, int passengers) {
    Modules modules{users.data_directory(), audit_log};
    rrs::BookingRequest request;
    request.train_number = 12345;
    request.class_code = "SL";
    request.journey_date = std::string(kJourneyDate);
    for (int i = 0; i < passengers; ++i) {
        request.passengers.push_back({"Passenger " + std::to_string(i + 1), 30 + i,
                                      rrs::Gender::Female});
    }
    request.payment = {rrs::PaymentOutcome::success, "PAY-0001"};
    const auto booked =
        modules.booking.book({"passenger1", rrs::Role::Passenger}, kSetupTime, request);
    REQUIRE(booked.kind == rrs::OutcomeKind::Success);
    return booked.detail;
}

struct Run {
    std::ostringstream out;
    std::ostringstream err;
};

// Passenger menu: 5 Cancel ticket, 7 Logout. The clock reads `now` for the whole session.
void run_session(const std::string& input, std::string_view now, const rrs::UserStore& users,
                 const rrs::AuditLog& audit_log, Run& run) {
    std::istringstream in(input);
    const rrs::AppContext context{in, run.out, run.err, users, audit_log,
                                  [now] { return std::string(now); }, {}};
    rrs::MenuRouter router;
    rrs::register_account_flows(router);
    rrs::register_cancel_flows(router);
    rrs::login_and_run_session(context, router);
}

std::string cancel_input(const std::string& pnr, const std::string& answers) {
    return "passenger1\nPass1Secure\n5\n" + pnr + "\n" + answers + "7\n";
}

}  // namespace

TEST_CASE("TC-CAN-01: a booking cancelled 50 hours before departure succeeds once",
          "[integration][M15][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    const auto pnr = book(users, audit_log, 4);

    Run first;
    run_session(cancel_input(pnr, "y\n\n"), k50HoursBefore, users, audit_log, first);
    const auto text = first.out.str();
    CHECK(text.find("Departure: 2026-11-02 06:00 | slab S1 | refund Rs 1620.00 (90 per cent)") !=
          std::string::npos);
    CHECK(text.find("OK: booking cancelled (ref: " + pnr + ")") != std::string::npos);
    CHECK(text.find("OK: refund Rs 1620.00 is PENDING settlement (ref: " + pnr + ")") !=
          std::string::npos);

    // Double cancellation is impossible; the existing record is shown instead (B3).
    Run second;
    run_session(cancel_input(pnr, ""), k50HoursBefore, users, audit_log, second);
    CHECK(second.out.str().find("Status: CANCELLED") != std::string::npos);
    CHECK(second.out.str().find("Cancelled at 2026-10-31T04:00:00Z | slab S1 | refund Rs 1620.00 | PENDING") !=
          std::string::npos);
    CHECK(second.out.str().find("REFUSED: booking is already cancelled") != std::string::npos);

    Modules modules{dir.path(), audit_log};
    CHECK(modules.booking.find_booking(pnr)->status == rrs::BookingStatus::Cancelled);
    CHECK(rrs::read_cancellations(modules.store).size() == 1);
}

TEST_CASE("TC-CAN-01: a cancellation 3 hours before departure is refused citing slab S4",
          "[integration][M15][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    const auto pnr = book(users, audit_log, 4);

    Run run;
    run_session(cancel_input(pnr, ""), k3HoursBefore, users, audit_log, run);
    CHECK(run.out.str().find(
              "REFUSED: slab S4: cancellation closes 4 hours before departure at 2026-11-02 06:00") !=
          std::string::npos);

    Modules modules{dir.path(), audit_log};
    CHECK(modules.booking.find_booking(pnr)->status == rrs::BookingStatus::Confirmed);
    CHECK(rrs::read_cancellations(modules.store).empty());
    CHECK(modules.free_sl() == 68);
}

TEST_CASE("TC-CAN-02: 180000 paise cancelled in slab S2 refunds 90000 and frees four seats",
          "[integration][M8][M15][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    const auto pnr = book(users, audit_log, 4);
    REQUIRE(Modules{dir.path(), audit_log}.free_sl() == 68);

    Run run;
    run_session(cancel_input(pnr, "y\n\n"), k20HoursBefore, users, audit_log, run);
    CHECK(run.out.str().find("slab S2 | refund Rs 900.00 (50 per cent)") != std::string::npos);

    Modules modules{dir.path(), audit_log};
    const auto cancellation = modules.cancellations.find_cancellation(pnr);
    REQUIRE(cancellation);
    CHECK(cancellation->refund_paise == 90000);
    CHECK(cancellation->slab == rrs::RefundSlab::S2);
    CHECK(modules.free_sl() == 72);
}

TEST_CASE("TC-CAN-03: a refund stays PENDING until its disbursement reference is recorded",
          "[integration][M15][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    const auto settled_now = book(users, audit_log, 4);
    const auto settled_later = book(users, audit_log, 2);

    // A reference given at M10 settles at once; an oversized one is asked again.
    Run run;
    run_session(cancel_input(settled_now, "y\n" + std::string(33, 'D') + "\nDISB-0001\n"),
                k20HoursBefore, users, audit_log, run);
    CHECK(run.out.str().find("INVALID disbursement reference") != std::string::npos);
    CHECK(run.out.str().find("OK: refund Rs 900.00 SETTLED (ref: " + settled_now + ")") !=
          std::string::npos);

    Run pending;
    run_session(cancel_input(settled_later, "y\n\n"), k20HoursBefore, users, audit_log, pending);

    Modules modules{dir.path(), audit_log};
    const auto records = rrs::read_cancellations(modules.store);
    REQUIRE(records.size() == 2);  // exactly one record per cancelled booking
    CHECK(modules.cancellations.find_cancellation(settled_now)->status() ==
          rrs::RefundStatus::Settled);
    CHECK(modules.cancellations.find_cancellation(settled_now)->disbursement_reference ==
          "DISB-0001");
    CHECK(modules.cancellations.find_cancellation(settled_later)->status() ==
          rrs::RefundStatus::Pending);

    // The operator settles the pending refund later; a second settlement is refused.
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    CHECK(modules.cancellations.settle_refund(admin, k20HoursBefore, settled_later, "DISB-0002")
              .kind == rrs::OutcomeKind::Success);
    const auto again =
        modules.cancellations.settle_refund(admin, k20HoursBefore, settled_later, "DISB-0003");
    CHECK(again.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(again.message == "refund is already settled");
    CHECK(modules.cancellations.find_cancellation(settled_later)->disbursement_reference ==
          "DISB-0002");
}

TEST_CASE("UC-02 B6: declining after seeing the refund leaves the booking unchanged",
          "[integration][M15][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    const auto pnr = book(users, audit_log, 2);

    Run run;
    run_session(cancel_input(pnr, "maybe\nn\n"), k50HoursBefore, users, audit_log, run);
    CHECK(run.out.str().find("INVALID confirmation: enter y or n") != std::string::npos);
    CHECK(run.out.str().find("REFUSED: cancellation declined; booking unchanged") !=
          std::string::npos);

    Modules modules{dir.path(), audit_log};
    CHECK(modules.booking.find_booking(pnr)->status == rrs::BookingStatus::Confirmed);
    CHECK(rrs::read_cancellations(modules.store).empty());
    CHECK(modules.free_sl() == 70);
}

TEST_CASE("UC-02 B2: another passenger cannot cancel; an administrator can",
          "[integration][M15][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    const auto pnr = book(users, audit_log, 2);

    Run run;
    run_session("passenger2\nPass2Secure\n5\n" + pnr + "\n7\n", k50HoursBefore, users, audit_log,
                run);
    CHECK(run.out.str().find("REFUSED: no such booking") != std::string::npos);
    CHECK(run.out.str().find("Passenger 1") == std::string::npos);

    Modules modules{dir.path(), audit_log};
    const auto cancelled =
        modules.cancellations.cancel({"admin", rrs::Role::Administrator}, k50HoursBefore, pnr);
    CHECK(cancelled.kind == rrs::OutcomeKind::Success);
    CHECK(modules.free_sl() == 72);
}
