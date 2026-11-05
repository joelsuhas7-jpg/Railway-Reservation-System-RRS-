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

#include "admin_flow.hpp"
#include "auth_flow.hpp"
#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-class-flow-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create class flow directory",
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

}  // namespace

TEST_CASE("TC-CLASS-01: administrator adds, updates and enquires class capacity",
          "[integration][M1][M3][M4][M5][M6][M7][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log{temp.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    rrs::TrainCatalogueModule trains{rrs::PersistenceStore{temp.path()}, audit_log};
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);

    std::istringstream in("2\nadmin\nAdminPassword1\n2\n1\n12345\nSL\n72\n"
                          "2\n12345\nSL\n80\n3\n12345\n4\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    const rrs::InteractiveSessionActionHandler actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            if (action == rrs::MenuAction::ManageClasses) {
                return rrs::manage_classes_from_console(
                    in, out, err, users, audit_log, session,
                    [] { return std::string(kTimestamp); }, session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions);
        }
        return rrs::Outcome::success("exit");
    });

    const rrs::ClassFareConfigModule reopened{rrs::PersistenceStore{temp.path()}, trains,
                                               audit_log,
                                               [] { return std::string(kTimestamp); }};
    const auto result = reopened.find_class(12345, "SL");
    REQUIRE(result.train_exists);
    REQUIRE(result.record.has_value());
    CHECK(result.record->capacity == 80);
    CHECK(out.str().find("SL\t80") != std::string::npos);
    CHECK(out.str().find("returned from class management") != std::string::npos);
    CHECK(err.str().empty());
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_ADDED\t12345");
    CHECK(lines[1] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tLOGIN_SUCCESS\tadmin");
    CHECK(lines[2] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tCLASS_ADDED\t12345:SL");
    CHECK(lines[3] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tCLASS_UPDATED\t12345:SL");
}

TEST_CASE("TC-CLASS-01: ManageClasses timeout prevents mutation and returns to public login",
          "[integration][M4][M6][M7][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log{temp.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    rrs::TrainCatalogueModule trains{rrs::PersistenceStore{temp.path()}, audit_log};
    REQUIRE(trains.add_train(rrs::Session{"admin", rrs::Role::Administrator}, kTimestamp,
                             valid_train()).kind == rrs::OutcomeKind::Success);

    std::istringstream in("2\nadmin\nAdminPassword1\n2\n1\n12345\nSL\n"
                          "2\nadmin\nAdminPassword1\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    std::size_t clock_calls = 0;
    const rrs::SessionClockProvider clock = [&clock_calls] {
        ++clock_calls;
        return std::chrono::steady_clock::time_point{} +
               (clock_calls >= 9 ? std::chrono::minutes(16) : std::chrono::minutes(0));
    };
    const rrs::InteractiveSessionActionHandler actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            if (action == rrs::MenuAction::ManageClasses) {
                return rrs::manage_classes_from_console(
                    in, out, err, users, audit_log, session,
                    [] { return std::string(kTimestamp); }, session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions,
                                              clock);
        }
        return rrs::Outcome::success("exit");
    });

    const rrs::ClassFareConfigModule classes{rrs::PersistenceStore{temp.path()}, trains,
                                              audit_log,
                                              [] { return std::string(kTimestamp); }};
    CHECK(classes.list_classes(12345).records.empty());
    CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kClassesFile));
    CHECK(out.str().find("session expired after 15 minutes of inactivity; please log in again") !=
          std::string::npos);
    const auto output = out.str();
    const auto first_login = output.find("OK: logged in (ref: admin)");
    REQUIRE(first_login != std::string::npos);
    CHECK(output.find("OK: logged in (ref: admin)", first_login + 1) != std::string::npos);
    CHECK(err.str().empty());
    const auto lines = read_lines(temp.path() / "audit.log");
    CHECK(std::count_if(lines.begin(), lines.end(), [](const std::string& line) {
              return line.find("\tLOGIN_SUCCESS\t") != std::string::npos;
          }) == 2);
}

TEST_CASE("TC-CLASS-01: ManageClasses rejects invalid choices and handles end of input",
          "[integration][M4][M7][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log{temp.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    std::istringstream in("2\nadmin\nAdminPassword1\n2\n9\n");
    std::ostringstream out;
    std::ostringstream err;
    const rrs::InteractiveSessionActionHandler actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            if (action == rrs::MenuAction::ManageClasses) {
                return rrs::manage_classes_from_console(
                    in, out, err, users, audit_log, session,
                    [] { return std::string(kTimestamp); }, session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions);
        }
        return rrs::Outcome::success("exit");
    });

    CHECK(out.str().find("INVALID menu selection") != std::string::npos);
    CHECK(out.str().find("class management cancelled") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kClassesFile));
    CHECK(err.str().empty());
}
