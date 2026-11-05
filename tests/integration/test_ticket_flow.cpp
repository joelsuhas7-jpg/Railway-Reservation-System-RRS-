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

#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "booking.hpp"
#include "class_fare_config.hpp"
#include "fare_calculation.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "seat_inventory.hpp"
#include "session_context.hpp"
#include "ticket_flow.hpp"
#include "ticket_retrieval.hpp"
#include "train_catalogue.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-ticket-flow-" + std::to_string(nonce) + "-" +
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

// Known-answer fixture: train 12345 with SL(72) at 45000 paise, two passengers and one booking
// of two SL seats made by passenger1. Returns the booking's PNR.
std::string seed_booking(const rrs::UserStore& users, const rrs::AuditLog& audit_log) {
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger2", "Pass2Secure", "p2@example.test"));
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
    rrs::FareConfigModule fares{store, trains, classes, audit_log, timestamp};
    REQUIRE(fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-30").kind ==
            rrs::OutcomeKind::Success);
    rrs::SeatInventoryModule inventory{store, classes, audit_log, timestamp};
    const rrs::FareCalculationModule calculation{fares};
    rrs::BookingModule booking{store, calculation, inventory, audit_log};

    rrs::BookingRequest request;
    request.train_number = 12345;
    request.class_code = "SL";
    request.journey_date = std::string(kJourneyDate);
    request.passengers = {{"Asha Rao", 34, rrs::Gender::Female},
                          {"Ravi Rao", 36, rrs::Gender::Male}};
    request.payment = {rrs::PaymentOutcome::success, "PAY-0001"};
    const auto booked = booking.book({"passenger1", rrs::Role::Passenger}, kTimestamp, request);
    REQUIRE(booked.kind == rrs::OutcomeKind::Success);
    return booked.detail;
}

struct Run {
    std::ostringstream out;
    std::ostringstream err;
};

// Passenger menu: 4 View ticket by PNR, 7 Logout.
void view_ticket(const std::string& login, const std::string& pnr, const rrs::UserStore& users,
                 const rrs::AuditLog& audit_log, Run& run) {
    std::istringstream in(login + "\n4\n" + pnr + "\n7\n");
    const rrs::AppContext context{in, run.out, run.err, users, audit_log,
                                  [] { return std::string(kTimestamp); }, {}};
    rrs::MenuRouter router;
    rrs::register_account_flows(router);
    rrs::register_ticket_flows(router);
    rrs::login_and_run_session(context, router);
}

std::string read_audit(const std::filesystem::path& directory) {
    std::ifstream file(directory / "audit.log");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string to_lower(std::string text) {
    for (auto& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

}  // namespace

TEST_CASE("TC-VIEW-01: the owner sees every field of the ticket", "[integration][M14][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    const auto pnr = seed_booking(users, audit_log);

    // The PNR is accepted in lower case.
    Run run;
    view_ticket("passenger1\nPass1Secure", to_lower(pnr), users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("PNR: " + pnr) != std::string::npos);
    CHECK(text.find("Train 12345 | Class SL | Journey date 2026-11-02") != std::string::npos);
    CHECK(text.find("Asha Rao | 34 | Female | SL-1") != std::string::npos);
    CHECK(text.find("Ravi Rao | 36 | Male | SL-2") != std::string::npos);
    CHECK(text.find("Total fare: Rs 900.00") != std::string::npos);
    CHECK(text.find("Status: CONFIRMED") != std::string::npos);
    CHECK(text.find("OK: ticket shown (ref: " + pnr + ")") != std::string::npos);
    CHECK(run.err.str().empty());
}

TEST_CASE("TC-VIEW-01: another passenger's PNR is refused as no such booking and audited",
          "[integration][M14][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    const auto pnr = seed_booking(users, audit_log);

    Run run;
    view_ticket("passenger2\nPass2Secure", pnr, users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("REFUSED: no such booking") != std::string::npos);
    CHECK(text.find("Asha Rao") == std::string::npos);
    CHECK(read_audit(dir.path()).find("passenger2\tPASSENGER\tTICKET_ACCESS_REFUSED\t" + pnr) !=
          std::string::npos);
}

TEST_CASE("TC-VIEW-01: unknown and malformed PNRs give the same answer",
          "[integration][M14][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_booking(users, audit_log);

    Run unknown;
    view_ticket("passenger1\nPass1Secure", "ZZZZZZZZZZ", users, audit_log, unknown);
    Run malformed;
    view_ticket("passenger1\nPass1Secure", "PNR?", users, audit_log, malformed);

    CHECK(unknown.out.str().find("REFUSED: no such booking") != std::string::npos);
    CHECK(malformed.out.str().find("REFUSED: no such booking") != std::string::npos);
}

TEST_CASE("TC-VIEW-01: an administrator may retrieve any passenger's ticket", "[integration][M14]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    const auto pnr = seed_booking(users, audit_log);
    const rrs::TicketRetrievalModule tickets{rrs::PersistenceStore{dir.path()}, audit_log};

    const auto lookup =
        tickets.retrieve({"admin", rrs::Role::Administrator}, kTimestamp, pnr);
    REQUIRE(lookup.outcome.kind == rrs::OutcomeKind::Success);
    REQUIRE(lookup.booking);
    CHECK(lookup.booking->user_id == "passenger1");
    CHECK(lookup.booking->seat_numbers.size() == 2);
    CHECK(lookup.booking->total_fare_paise == 90000);
}
