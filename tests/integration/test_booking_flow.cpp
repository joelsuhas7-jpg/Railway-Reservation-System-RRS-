#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "booking.hpp"
#include "booking_flow.hpp"
#include "class_fare_config.hpp"
#include "fare_calculation.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "search_flow.hpp"
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
                    ("rrs-booking-flow-" + std::to_string(nonce) + "-" +
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

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";
constexpr std::string_view kJourneyDate = "2026-11-02";

// Known-answer fixture: train 12345 SBC to NDLS daily, SL(72) at 45000 paise, 3A(64) with no fare.
void seed_catalogue(const rrs::UserStore& users, const rrs::AuditLog& audit_log) {
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));
    const rrs::PersistenceStore store{users.data_directory()};
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const auto timestamp = [] { return std::string(kTimestamp); };
    rrs::TrainCatalogueModule trains{store, audit_log};
    REQUIRE(trains
                .add_train(admin, kTimestamp,
                           {12345, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f})
                .kind == rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes{store, trains, audit_log, timestamp};
    REQUIRE(classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(classes.add_class(admin, kTimestamp, 12345, "3A", 64).kind ==
            rrs::OutcomeKind::Success);
    rrs::FareConfigModule fares{store, trains, classes, audit_log, timestamp};
    REQUIRE(fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-30").kind ==
            rrs::OutcomeKind::Success);
}

// The M6 to M13 modules over the test directory, for checks made outside the console.
struct Modules {
    explicit Modules(const std::filesystem::path& directory, const rrs::AuditLog& audit_log)
        : store{directory},
          trains{store, audit_log},
          classes{store, trains, audit_log, [] { return std::string(kTimestamp); }},
          fares{store, trains, classes, audit_log, [] { return std::string(kTimestamp); }},
          inventory{store, classes, audit_log, [] { return std::string(kTimestamp); }},
          calculation{fares},
          booking{store, calculation, inventory, audit_log} {}

    std::uint32_t free_seats(std::string_view class_code) {
        return inventory.get_inventory(12345, class_code, kJourneyDate).free_seats;
    }

    rrs::PersistenceStore store;
    rrs::TrainCatalogueModule trains;
    rrs::ClassFareConfigModule classes;
    rrs::FareConfigModule fares;
    rrs::SeatInventoryModule inventory;
    rrs::FareCalculationModule calculation;
    rrs::BookingModule booking;
};

struct Run {
    std::ostringstream out;
    std::ostringstream err;
};

// Logs in with the given input and serves the role menu through the production registrations.
void run_session(const std::string& input, const rrs::UserStore& users,
                 const rrs::AuditLog& audit_log, Run& run) {
    std::istringstream in(input);
    const rrs::AppContext context{in, run.out, run.err, users, audit_log,
                                  [] { return std::string(kTimestamp); }, {}};
    rrs::MenuRouter router;
    rrs::register_account_flows(router);
    rrs::register_search_flows(router);
    rrs::register_booking_flows(router);
    rrs::login_and_run_session(context, router);
}

std::string passenger_lines(int count) {
    std::string lines;
    for (int i = 1; i <= count; ++i) {
        lines += "Passenger " + std::to_string(i) + "\n" + std::to_string(20 + i) + "\nf\n";
    }
    return lines;
}

// Passenger menu: 2 View train detail, 3 Book ticket, 7 Logout.
std::string booking_input(const std::string& class_code, const std::string& count,
                          const std::string& passengers, const std::string& tail) {
    return "passenger1\nPass1Secure\n3\n12345\n" + std::string(kJourneyDate) + "\n" + class_code +
           "\n" + count + "\n" + passengers + tail;
}

std::string read_audit(const std::filesystem::path& directory) {
    std::ifstream file(directory / "audit.log");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("TC-FARE-02: four SL passengers pay 180000 paise and hold four seats",
          "[integration][M11][M13][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    Run run;
    run_session(booking_input("sl", "4", passenger_lines(4), "y\nsuccess\nPAY-0001\n7\n"), users,
                audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("SL | Rs 450.00 | 72") != std::string::npos);
    CHECK(text.find("Total fare: Rs 1800.00 (4 x Rs 450.00)") != std::string::npos);
    CHECK(text.find("OK: booking confirmed (ref: ") != std::string::npos);
    CHECK(text.find("Passenger 4 | 24 | Female | SL-4") != std::string::npos);
    CHECK(text.find("Status: CONFIRMED") != std::string::npos);
    CHECK(run.err.str().empty());

    Modules modules{dir.path(), audit_log};
    const auto bookings = modules.booking.list_bookings();
    REQUIRE(bookings.size() == 1);
    CHECK(rrs::is_valid_pnr(bookings[0].pnr));
    CHECK(bookings[0].total_fare_paise == 180000);
    CHECK(bookings[0].user_id == "passenger1");
    CHECK(bookings[0].payment_reference == "PAY-0001");
    CHECK(text.find("PNR: " + bookings[0].pnr) != std::string::npos);
    CHECK(modules.free_seats("SL") == 68);
    CHECK(read_audit(dir.path()).find("BOOKING_CONFIRMED\t" + bookings[0].pnr) !=
          std::string::npos);

    // The next search shows the reduced free count.
    Run detail;
    run_session("passenger1\nPass1Secure\n2\n12345\n" + std::string(kJourneyDate) + "\n7\n",
                users, audit_log, detail);
    CHECK(detail.out.str().find("SL | Rs 450.00 | 68") != std::string::npos);
}

TEST_CASE("TC-FARE-02: a class without a configured fare is refused before any passenger entry",
          "[integration][M11][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    Run run;
    run_session(booking_input("3A", "2", "", "7\n"), users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("REFUSED: class not open for booking") != std::string::npos);
    CHECK(text.find("Passenger 1 of") == std::string::npos);
    CHECK(text.find("Total fare") == std::string::npos);
    CHECK(Modules{dir.path(), audit_log}.booking.list_bookings().empty());
}

TEST_CASE("TC-BOOK-01: counts and ages out of range are refused naming the field",
          "[integration][M10][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    // 7 passengers and 0 are re-prompted; ages 0 and 121 are re-prompted for that field alone.
    const std::string passengers =
        "Asha Rao\n0\n121\n34\nf\n" + passenger_lines(5);
    Run run;
    run_session(booking_input("SL", "7\n0\n6", passengers, "n\n7\n"), users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("INVALID passenger count") != std::string::npos);
    CHECK(text.find("INVALID age") != std::string::npos);
    CHECK(text.find("Passenger 6 of 6") != std::string::npos);
    CHECK(text.find("Total fare: Rs 2700.00 (6 x Rs 450.00)") != std::string::npos);
    CHECK(text.find("REFUSED: booking discarded; no seats were held") != std::string::npos);

    Modules modules{dir.path(), audit_log};
    CHECK(modules.booking.list_bookings().empty());
    CHECK(modules.free_seats("SL") == 72);
}

TEST_CASE("TC-BOOK-01: the booking module refuses a seven-passenger block", "[integration][M10][M13]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    Modules modules{dir.path(), audit_log};

    rrs::BookingRequest request;
    request.train_number = 12345;
    request.class_code = "SL";
    request.journey_date = std::string(kJourneyDate);
    request.passengers.assign(7, {"Asha Rao", 34, rrs::Gender::Female});
    request.payment = {rrs::PaymentOutcome::success, "PAY-0007"};
    const rrs::Session passenger{"passenger1", rrs::Role::Passenger};

    const auto outcome = modules.booking.book(passenger, kTimestamp, request);
    CHECK(outcome.kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(outcome.detail == "passengers");
    CHECK(modules.booking.list_bookings().empty());
}

TEST_CASE("TC-PAY-01: a failed payment leaves no booking, no PNR and no seat allocation",
          "[integration][M12][M13][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    Run run;
    run_session(booking_input("SL", "2", passenger_lines(2), "y\nfailure\nPAY-FAIL-1\n7\n"),
                users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("REFUSED: payment failed; booking rejected and no seats allocated") !=
          std::string::npos);
    CHECK(text.find("PNR:") == std::string::npos);

    Modules modules{dir.path(), audit_log};
    CHECK(modules.booking.list_bookings().empty());
    CHECK(modules.free_seats("SL") == 72);
    CHECK(read_audit(dir.path()).find("BOOKING_REJECTED\tPAY-FAIL-1") != std::string::npos);
}

TEST_CASE("RRS-F-016: the check inside the lock refuses a booking the free seats cannot cover",
          "[integration][M8][M13]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);
    Modules modules{dir.path(), audit_log};
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const rrs::Session passenger{"passenger1", rrs::Role::Passenger};

    rrs::BookingRequest request;
    request.train_number = 12345;
    request.class_code = "SL";
    request.journey_date = std::string(kJourneyDate);
    request.passengers = {{"Asha Rao", 34, rrs::Gender::Female},
                          {"Ravi Rao", 36, rrs::Gender::Male}};
    request.payment = {rrs::PaymentOutcome::success, "PAY-0001"};

    const auto first = modules.booking.book(passenger, kTimestamp, request);
    REQUIRE(first.kind == rrs::OutcomeKind::Success);
    request.payment.reference = "PAY-0002";
    const auto second = modules.booking.book(passenger, kTimestamp, request);
    REQUIRE(second.kind == rrs::OutcomeKind::Success);
    CHECK(first.detail != second.detail);
    CHECK(modules.booking.find_booking(second.detail)->seat_numbers ==
          std::vector<std::uint32_t>{3, 4});

    // 72 - 4 booked - 67 blocked leaves one seat for a two-passenger request.
    REQUIRE(modules.inventory
                .block_seats(admin, kTimestamp, 12345, "SL", kJourneyDate, 67)
                .kind == rrs::OutcomeKind::Success);
    request.payment.reference = "PAY-0003";
    const auto refused = modules.booking.book(passenger, kTimestamp, request);
    CHECK(refused.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(refused.message == "insufficient seats: 2 requested, 1 available");
    CHECK(modules.booking.list_bookings().size() == 2);
    CHECK(modules.free_seats("SL") == 1);
}
