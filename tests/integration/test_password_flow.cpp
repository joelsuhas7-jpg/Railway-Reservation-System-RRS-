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

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-password-flow-" + std::to_string(nonce) + "-" +
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

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

bool has_iso_utc_timestamp(const std::string& value) {
    constexpr std::string_view format = "dddd-dd-ddTdd:dd:ddZ";
    if (value.size() < format.size()) {
        return false;
    }

    for (std::size_t index = 0; index < format.size(); ++index) {
        const char character = value[index];
        const char expected = format[index];
        if (expected == 'd') {
            if (character < '0' || character > '9') {
                return false;
            }
        } else if (character != expected) {
            return false;
        }
    }
    return true;
}

// Runs the public menu as main does (1 Register, 2 Login, 3 Exit) and returns the output.
constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";

rrs::AuditTimestampProvider fixed_timestamp_provider() {
    return [] { return std::string(kTimestamp); };
}

std::string run_public_menu(const std::string& input, const rrs::UserStore& users,
                            const rrs::AuditLog& audit_log) {
    std::istringstream in(input);
    std::ostringstream out;
    std::ostringstream err;
    const rrs::ActionHandler not_yet = [](rrs::MenuAction) {
        return rrs::Outcome::refusal("this feature is not available yet");
    };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              fixed_timestamp_provider(), not_yet);
        }
        return rrs::register_from_console(in, out, users, audit_log,
                                          fixed_timestamp_provider());
    });
    return out.str();
}

}  // namespace

TEST_CASE("TC-AUTH-04: change, then reset, through the menus; each is audited",
          "[integration][M4][M5][M3][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));

    // Passenger: Change password is item 6, Logout 7. Administrator: Reset is item 7, Logout 8.
    const auto text = run_public_menu("2\npassenger1\nPass1Secure\n6\nPass1Secure\nFresh2Secret\n7\n"
                                      "2\nadmin\nAdminPassword1\n7\npassenger1\nReset3Secret\n8\n"
                                      "3\n",
                                      users, audit_log);

    CHECK(text.find("OK: password changed (ref: passenger1)") != std::string::npos);
    CHECK(text.find("OK: passenger password reset (ref: passenger1)") != std::string::npos);
    CHECK_FALSE(rrs::authenticate(users, "passenger1", "Pass1Secure").session);
    CHECK_FALSE(rrs::authenticate(users, "passenger1", "Fresh2Secret").session);
    CHECK(rrs::authenticate(users, "passenger1", "Reset3Secret").session);

    const auto lines = read_lines(dir.path() / "audit.log");
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == std::string(kTimestamp) +
                          "\tpassenger1\tPASSENGER\tLOGIN_SUCCESS\tpassenger1");
    CHECK(lines[1].find('\t') == 20);
    CHECK(has_iso_utc_timestamp(lines[1]));
    CHECK(lines[1].substr(20) ==
          "\tpassenger1\tPASSENGER\tPASSWORD_CHANGE\tpassenger1");
    CHECK(lines[2] == std::string(kTimestamp) + "\tadmin\tADMIN\tLOGIN_SUCCESS\tadmin");
    CHECK(lines[3].find('\t') == 20);
    CHECK(has_iso_utc_timestamp(lines[3]));
    CHECK(lines[3].substr(20) == "\tadmin\tADMIN\tPASSWORD_RESET\tpassenger1");
    for (const auto& line : lines) {
        CHECK(line.find("Fresh2Secret") == std::string::npos);
        CHECK(line.find("Reset3Secret") == std::string::npos);
    }
}

TEST_CASE("TC-AUTH-04: refused changes are not audited and keep the old password",
          "[integration][M4][M3][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const rrs::AuditLog audit_log{dir.path() / "audit.log"};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));

    const auto text = run_public_menu("2\npassenger1\nPass1Secure\n6\nWrong1Pass\nFresh2Secret\n"
                                      "6\nPass1Secure\nweak\n7\n"
                                      "2\nadmin\nAdminPassword1\n7\nnobody\nReset3Secret\n8\n"
                                      "3\n",
                                      users, audit_log);

    CHECK(text.find("REFUSED: current password is incorrect") != std::string::npos);
    CHECK(text.find("INVALID new password:") != std::string::npos);
    CHECK(text.find("REFUSED: no such passenger account") != std::string::npos);
    CHECK(rrs::authenticate(users, "passenger1", "Pass1Secure").session);
    const auto lines = read_lines(dir.path() / "audit.log");
    CHECK(std::none_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("\tPASSWORD_CHANGE\t") != std::string::npos ||
               line.find("\tPASSWORD_RESET\t") != std::string::npos;
    }));
}

TEST_CASE("RRS-F-005-01: audit timestamps are ISO 8601 UTC", "[unit][M17]") {
    CHECK(rrs::format_utc_timestamp(0) == "1970-01-01T00:00:00Z");
    CHECK(rrs::format_utc_timestamp(951782400) == "2000-02-29T00:00:00Z");
    CHECK(rrs::format_utc_timestamp(1793000000) == "2026-10-26T07:33:20Z");
    CHECK(rrs::format_utc_timestamp(4102444799) == "2099-12-31T23:59:59Z");
    CHECK(rrs::format_utc_timestamp(-1) == "1969-12-31T23:59:59Z");
    CHECK(rrs::utc_timestamp_now().size() == 20);
}
