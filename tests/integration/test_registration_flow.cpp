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

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-registration-flow-" + std::to_string(nonce) + "-" +
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

}  // namespace

namespace {

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";

rrs::AuditTimestampProvider fixed_timestamp_provider() {
    return [] { return std::string(kTimestamp); };
}

}  // namespace

TEST_CASE("TC-AUTH-03: first-run console setup retries until one administrator exists",
          "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    std::istringstream in("admin\nshort1\nadmin@example.test\n"
                          "admin\nAdminPassword1\nadmin@example.test\n");
    std::ostringstream out;
    std::ostringstream err;

    const rrs::AuditLog audit_log(dir.path() / "audit.log");
    REQUIRE(rrs::run_bootstrap(in, out, err, users, audit_log,
                               fixed_timestamp_provider()));
    CHECK(out.str().find("INVALID password") != std::string::npos);
    CHECK(out.str().find("OK: administrator account created") != std::string::npos);
    CHECK(users.count() == 1);

    std::istringstream unused("intruder\nIntruder1Pass\nx@example.test\n");
    CHECK(rrs::run_bootstrap(unused, out, err, users, audit_log,
                             fixed_timestamp_provider()));
    CHECK(users.count() == 1);
    CHECK_FALSE(users.find("intruder").has_value());
}

TEST_CASE("TC-AUTH-03: setup ends cleanly when input ends", "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    std::istringstream in("3\n");
    std::ostringstream out;
    std::ostringstream err;
    const rrs::AuditLog audit_log(dir.path() / "audit.log");
    CHECK_FALSE(rrs::run_bootstrap(in, out, err, users, audit_log,
                                   fixed_timestamp_provider()));
    CHECK(rrs::bootstrap_required(users));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "audit.log"));
}

TEST_CASE("TC-SEC-05: first administrator creation is audited; rejected bootstrap is not",
          "[integration][M3][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    std::istringstream input("admin\nAdminPassword1\nadmin@example.test\n");
    std::ostringstream out;
    std::ostringstream err;

    REQUIRE(rrs::run_bootstrap(input, out, err, users, audit_log,
                               fixed_timestamp_provider()));

    std::ifstream audit_file(dir.path() / "audit.log", std::ios::binary);
    const std::string audit((std::istreambuf_iterator<char>(audit_file)),
                            std::istreambuf_iterator<char>());
    CHECK(audit == std::string(kTimestamp) +
                       "\tadmin\tADMIN\tADMIN_BOOTSTRAPPED\tadmin\n");

    std::istringstream rejected_input("intruder\nShort1\nx@example.test\n");
    TempDirectory rejected_dir;
    const rrs::AuditLog isolated_rejected_log(rejected_dir.path() / "audit.log");
    CHECK_FALSE(rrs::run_bootstrap(rejected_input, out, err,
                                   rrs::UserStore{rrs::PersistenceStore{rejected_dir.path()}},
                                   isolated_rejected_log, fixed_timestamp_provider()));
    CHECK_FALSE(std::filesystem::exists(rejected_dir.path() / "audit.log"));
}

TEST_CASE("TC-REG-01: register from the public menu, then reject the duplicate",
          "[integration][M4][M17]") {
    TempDirectory dir;
    {
        const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
        REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    }
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};

    // Menu option 1 is Register; option 3 is Exit.
    std::istringstream in("1\npassenger1\nPass1Secure\np1@example.test\n"
                          "1\nPassenger1\nOther1Pass\nx@example.test\n"
                          "3\n");
    std::ostringstream out;
    std::ostringstream err;
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        REQUIRE(action == rrs::MenuAction::Register);
        return rrs::register_from_console(in, out, users, audit_log,
                                          fixed_timestamp_provider());
    });

    CHECK(out.str().find("OK: passenger account created (ref: passenger1)") != std::string::npos);
    CHECK(out.str().find("INVALID user id: is already registered") != std::string::npos);
    CHECK(err.str().empty());

    const rrs::UserStore reopened{rrs::PersistenceStore{dir.path()}};
    CHECK(reopened.count() == 2);
    const auto stored = reopened.find("passenger1");
    REQUIRE(stored.has_value());
    CHECK(stored->role == rrs::Role::Passenger);
}

TEST_CASE("TC-SEC-05: registration success is audited and duplicate registration is not",
          "[integration][M3][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));

    std::istringstream valid_input("passenger1\nPass1Secure\np1@example.test\n");
    std::ostringstream out;
    const auto registered =
        rrs::register_from_console(valid_input, out, users, audit_log,
                                   fixed_timestamp_provider());
    REQUIRE(registered.kind == rrs::OutcomeKind::Success);

    std::istringstream duplicate_input("passenger1\nOther1Pass\nx@example.test\n");
    const auto duplicate =
        rrs::register_from_console(duplicate_input, out, users, audit_log,
                                   fixed_timestamp_provider());
    CHECK(duplicate.kind == rrs::OutcomeKind::ValidationFailure);

    std::ifstream audit_file(dir.path() / "audit.log", std::ios::binary);
    const std::string audit((std::istreambuf_iterator<char>(audit_file)),
                            std::istreambuf_iterator<char>());
    CHECK(audit == std::string(kTimestamp) +
                       "\tpassenger1\tPASSENGER\tPASSENGER_REGISTERED\tpassenger1\n");
}

TEST_CASE("TC-REG-01: input ending mid-form registers nothing", "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    std::istringstream in("passenger1\nPass1Secure\n");
    std::ostringstream out;
    const auto outcome = rrs::register_from_console(in, out, users, audit_log,
                                                    fixed_timestamp_provider());
    CHECK(outcome.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(users.count() == 1);
}
