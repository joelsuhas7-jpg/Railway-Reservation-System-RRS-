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
#include "class_fare_config.hpp"
#include "fare_config.hpp"
#include "inventory_flow.hpp"
#include "persistence.hpp"
#include "search_flow.hpp"
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
                    ("rrs-search-console-" + std::to_string(nonce) + "-" +
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
    rrs::register_inventory_flows(router);
    rrs::register_search_flows(router);
    rrs::login_and_run_session(context, router);
}

}  // namespace

TEST_CASE("TC-SEARCH-01: the passenger menu searches and shows fare and free seats per class",
          "[integration][M9][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    // Passenger menu: 1 Search trains, 7 Logout. Station codes are accepted in lower case.
    Run run;
    run_session("passenger1\nPass1Secure\n1\nsbc\nndls\n" + std::string(kJourneyDate) + "\n7\n",
                users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("12345 - Karnataka Express | SBC to NDLS | 06:00-14:00") !=
          std::string::npos);
    CHECK(text.find("SL | Rs 450.00 | 72") != std::string::npos);
    CHECK(text.find("3A | not open | 64") != std::string::npos);
    CHECK(text.find("OK: 1 train found") != std::string::npos);
    CHECK(run.err.str().empty());
}

TEST_CASE("TC-SEARCH-01: an unserved route and invalid input are explained",
          "[integration][M9][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    Run run;
    run_session("passenger1\nPass1Secure\n1\nSBC\nMAS\n" + std::string(kJourneyDate) +
                    "\n1\nSBC\nNDLS\n2026-02-30\n7\n",
                users, audit_log, run);
    const auto text = run.out.str();

    CHECK(text.find("No train serves this route.") != std::string::npos);
    CHECK(text.find("OK: 0 trains found") != std::string::npos);
    CHECK(text.find("INVALID search: journey date must be a valid YYYY-MM-DD date") !=
          std::string::npos);
}

TEST_CASE("TC-SEAT-01: an administrator blocks seats and the next search shows the change",
          "[integration][M8][M9][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    // Administrator menu: 4 Adjust seat availability, 8 Logout.
    Run admin;
    run_session("admin\nAdminPassword1\n4\n12345\nSL\n" + std::string(kJourneyDate) +
                    "\n12\n8\n",
                users, audit_log, admin);
    CHECK(admin.out.str().find("OK: seats blocked; 60 free seats remain") != std::string::npos);

    // Passenger menu: 2 View train detail and class availability.
    Run passenger;
    run_session("passenger1\nPass1Secure\n2\n12345\n" + std::string(kJourneyDate) + "\n7\n",
                users, audit_log, passenger);
    const auto text = passenger.out.str();
    CHECK(text.find("SL | Rs 450.00 | 60") != std::string::npos);
    CHECK(text.find("OK: train detail shown (ref: 12345)") != std::string::npos);
}

TEST_CASE("TC-SEAT-01: blocking more seats than are free is refused without a change",
          "[integration][M8][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    seed_catalogue(users, audit_log);

    Run admin;
    run_session("admin\nAdminPassword1\n4\n12345\nSL\n" + std::string(kJourneyDate) +
                    "\n73\n4\n12345\nSL\n" + std::string(kJourneyDate) + "\nmany\n8\n",
                users, audit_log, admin);
    const auto text = admin.out.str();

    CHECK(text.find("REFUSED: cannot block more seats than are currently free") !=
          std::string::npos);
    CHECK(text.find("INVALID seats to block: enter a whole number") != std::string::npos);
    CHECK(text.find("OK: seats blocked") == std::string::npos);
}
