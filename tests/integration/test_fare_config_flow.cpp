#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "auth_flow.hpp"
#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr std::string_view kTimestamp = "2026-09-30T10:00:00Z";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-fare-flow-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create fare flow directory",
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

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }
    return lines;
}

rrs::TrainRecord valid_train() {
    return {12345, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f};
}

void seed_admin_train_and_class(const rrs::UserStore& users, const rrs::AuditLog& audit_log) {
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    rrs::TrainCatalogueModule trains{rrs::PersistenceStore{users.data_directory()}, audit_log};
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(trains.add_train(admin, kTimestamp, valid_train()).kind == rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes{rrs::PersistenceStore{users.data_directory()}, trains,
                                       audit_log, [] { return std::string(kTimestamp); }};
    REQUIRE(classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
}

rrs::InteractiveSessionActionHandler fare_actions(
    std::istream& in, std::ostream& out, std::ostream& err, const rrs::UserStore& users,
    const rrs::AuditLog& audit_log) {
    return [&](rrs::MenuAction action, const rrs::Session& session,
               const rrs::SessionActivityCheck& activity) {
        if (action == rrs::MenuAction::ManageFares) {
            return rrs::manage_fares_from_console(
                in, out, err, users, audit_log, session,
                [] { return std::string(kTimestamp); }, activity);
        }
        return rrs::Outcome::refusal("this feature is not available yet");
    };
}

}  // namespace

TEST_CASE("TC-FARE-01: authenticated administrator configures and lists 45000 paise",
          "[integration][M1][M3][M4][M5][M6][M7][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log{temp.path() / "audit.log"};
    seed_admin_train_and_class(users, audit_log);

    std::istringstream in("2\nadmin\nAdminPassword1\n3\n1\n12345\nSL\n450.00\n"
                          "2026-10-01\n2\n12345\nSL\n3\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    const auto actions = fare_actions(in, out, err, users, audit_log);
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions);
        }
        return rrs::Outcome::success("exit");
    });

    const rrs::PersistenceStore store{temp.path()};
    rrs::TrainCatalogueModule trains{store, audit_log};
    rrs::ClassFareConfigModule classes{store, trains, audit_log,
                                       [] { return std::string(kTimestamp); }};
    rrs::FareConfigModule fares{store, trains, classes, audit_log,
                                [] { return std::string(kTimestamp); }};
    const auto result = fares.find_applicable_fare(12345, "SL", "2026-10-01");
    REQUIRE(result.record.has_value());
    CHECK(result.record->fare_paise == 45000);
    CHECK(result.record->effective_from == "2026-10-01");
    CHECK(out.str().find("45000") != std::string::npos);
    CHECK(err.str().empty());
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 4);
    CHECK(lines[2] == "2026-09-30T10:00:00Z\tadmin\tADMIN\tLOGIN_SUCCESS\tadmin");
    CHECK(lines[3] ==
          "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_CONFIGURED\t12345:SL:2026-10-01");
}

TEST_CASE("TC-FARE-01: console explicitly selects which future fare to replace",
          "[integration][M1][M3][M5][M6][M7][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log{temp.path() / "audit.log"};
    seed_admin_train_and_class(users, audit_log);
    {
        const rrs::PersistenceStore store{temp.path()};
        rrs::TrainCatalogueModule trains{store, audit_log};
        rrs::ClassFareConfigModule classes{store, trains, audit_log,
                                           [] { return std::string(kTimestamp); }};
        rrs::FareConfigModule fares{store, trains, classes, audit_log,
                                    [] { return std::string(kTimestamp); }};
        const rrs::Session admin{"admin", rrs::Role::Administrator};
        REQUIRE(fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-01").kind ==
                rrs::OutcomeKind::Success);
        REQUIRE(fares.configure_fare(admin, 12345, "SL", 50000, "2026-10-10").kind ==
                rrs::OutcomeKind::Success);
    }

    std::istringstream in("2\nadmin\nAdminPassword1\n3\n1\n12345\nSL\n2\n550.00\n"
                          "2026-10-20\n2\n12345\nSL\n3\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    const auto actions = fare_actions(in, out, err, users, audit_log);
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions);
        }
        return rrs::Outcome::success("exit");
    });

    const rrs::PersistenceStore store{temp.path()};
    rrs::TrainCatalogueModule trains{store, audit_log};
    rrs::ClassFareConfigModule classes{store, trains, audit_log,
                                       [] { return std::string(kTimestamp); }};
    rrs::FareConfigModule fares{store, trains, classes, audit_log,
                                [] { return std::string(kTimestamp); }};
    const auto records = fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    CHECK(records[0].effective_from == "2026-10-01");
    CHECK(records[0].fare_paise == 45000);
    CHECK(records[1].effective_from == "2026-10-20");
    CHECK(records[1].fare_paise == 55000);
    CHECK(out.str().find("2026-10-01") != std::string::npos);
    CHECK(out.str().find("2026-10-10") != std::string::npos);
    CHECK(err.str().empty());
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 6);
    CHECK(lines.back() ==
          "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_UPDATED\t12345:SL:2026-10-20");
}

TEST_CASE("TC-FARE-01: ManageFares handles invalid choices, back and end of input",
          "[integration][M5][M7][M17]") {
    SECTION("invalid choice and back") {
        TempDirectory temp;
        const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
        const rrs::AuditLog audit_log{temp.path() / "audit.log"};
        seed_admin_train_and_class(users, audit_log);
        std::istringstream in("2\nadmin\nAdminPassword1\n3\n9\n3\n8\n3\n");
        std::ostringstream out;
        std::ostringstream err;
        const auto actions = fare_actions(in, out, err, users, audit_log);
        rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
            if (action == rrs::MenuAction::Login) {
                return rrs::login_and_run_session(in, out, err, users, audit_log,
                                                  [] { return std::string(kTimestamp); }, actions);
            }
            return rrs::Outcome::success("exit");
        });
        CHECK(out.str().find("INVALID menu selection") != std::string::npos);
        CHECK(out.str().find("returned from fare management") != std::string::npos);
        CHECK(err.str().empty());
        CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kFaresFile));
    }
    SECTION("end of input") {
        TempDirectory temp;
        const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
        const rrs::AuditLog audit_log{temp.path() / "audit.log"};
        seed_admin_train_and_class(users, audit_log);
        std::istringstream in("2\nadmin\nAdminPassword1\n3\n");
        std::ostringstream out;
        std::ostringstream err;
        const auto actions = fare_actions(in, out, err, users, audit_log);
        rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
            if (action == rrs::MenuAction::Login) {
                return rrs::login_and_run_session(in, out, err, users, audit_log,
                                                  [] { return std::string(kTimestamp); }, actions);
            }
            return rrs::Outcome::success("exit");
        });
        CHECK(out.str().find("fare management cancelled") != std::string::npos);
        CHECK(err.str().empty());
        CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kFaresFile));
    }
    SECTION("cancel during fare input") {
        TempDirectory temp;
        const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
        const rrs::AuditLog audit_log{temp.path() / "audit.log"};
        seed_admin_train_and_class(users, audit_log);
        std::istringstream in("2\nadmin\nAdminPassword1\n3\n1\n12345\nSL\ncancel\n"
                              "3\n8\n3\n");
        std::ostringstream out;
        std::ostringstream err;
        const auto actions = fare_actions(in, out, err, users, audit_log);
        rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
            if (action == rrs::MenuAction::Login) {
                return rrs::login_and_run_session(in, out, err, users, audit_log,
                                                  [] { return std::string(kTimestamp); }, actions);
            }
            return rrs::Outcome::success("exit");
        });
        CHECK(out.str().find("Fare operation cancelled") != std::string::npos);
        CHECK(err.str().empty());
        CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kFaresFile));
    }
}

TEST_CASE("TC-FARE-01: expired ManageFares session cannot persist a fare and requires relogin",
          "[integration][M4][M5][M7][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log{temp.path() / "audit.log"};
    seed_admin_train_and_class(users, audit_log);
    std::istringstream in("2\nadmin\nAdminPassword1\n3\n1\n12345\nSL\n450.00\n"
                          "2026-10-01\n2\nadmin\nAdminPassword1\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    std::size_t clock_calls = 0;
    const rrs::SessionClockProvider clock = [&clock_calls] {
        ++clock_calls;
        return std::chrono::steady_clock::time_point{} +
               (clock_calls >= 13 ? std::chrono::minutes(16) : std::chrono::minutes(0));
    };
    const auto actions = fare_actions(in, out, err, users, audit_log);
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions,
                                              clock);
        }
        return rrs::Outcome::success("exit");
    });

    CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kFaresFile));
    CHECK(out.str().find("session expired after 15 minutes of inactivity; please log in again") !=
          std::string::npos);
    const auto lines = read_lines(temp.path() / "audit.log");
    CHECK(std::count_if(lines.begin(), lines.end(), [](const std::string& line) {
              return line.find("\tLOGIN_SUCCESS\t") != std::string::npos;
          }) == 2);
    CHECK(std::count_if(lines.begin(), lines.end(), [](const std::string& line) {
              return line.find("\tSESSION_IDLE_TIMEOUT\t") != std::string::npos;
          }) == 1);
    CHECK(err.str().empty());
}
