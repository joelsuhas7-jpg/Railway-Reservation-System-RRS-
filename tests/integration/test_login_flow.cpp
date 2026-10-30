#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "auth_flow.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-login-flow-" + std::to_string(nonce) + "-" +
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

struct Session {
    std::ostringstream out;
    std::ostringstream err;
    std::vector<rrs::MenuAction> role_actions;
};

// Runs the public menu as main does: 1 Register, 2 Login, 3 Exit.
void run_public_menu(const std::string& input, const rrs::UserStore& users, Session& run) {
    std::istringstream in(input);
    const rrs::ActionHandler role_handler = [&run](rrs::MenuAction action) {
        run.role_actions.push_back(action);
        return rrs::Outcome::refusal("this feature is not available yet");
    };
    rrs::run_menu(in, run.out, run.err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, run.out, run.err, users, role_handler);
        }
        return rrs::register_from_console(in, run.out, users);
    });
}

std::size_t occurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("TC-AUTH-01: login opens the menu tree for the account's role",
          "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));

    // Register passenger1, log in as passenger (Logout is item 7), then as admin (item 8).
    Session run;
    run_public_menu("1\npassenger1\nPass1Secure\np1@example.test\n"
                    "2\npassenger1\nPass1Secure\n1\n7\n"
                    "2\nadmin\nAdminPassword1\n1\n8\n"
                    "3\n",
                    users, run);
    const auto text = run.out.str();

    CHECK(text.find("OK: logged in (ref: passenger1)") != std::string::npos);
    CHECK(text.find("OK: logged in (ref: admin)") != std::string::npos);
    CHECK(text.find(std::string(rrs::menu_title(rrs::MenuTree::Passenger))) != std::string::npos);
    CHECK(text.find(std::string(rrs::menu_title(rrs::MenuTree::Administrator))) !=
          std::string::npos);
    REQUIRE(run.role_actions.size() == 2);
    CHECK(run.role_actions[0] == rrs::MenuAction::SearchTrains);
    CHECK(run.role_actions[1] == rrs::MenuAction::ManageTrains);
    CHECK(run.err.str().empty());
}

TEST_CASE("TC-AUTH-01: failed logins never say which field was wrong",
          "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));

    Session run;
    run_public_menu("2\nadmin\nWrong1Password\n2\nnobody\nAdminPassword1\n3\n", users, run);
    const auto text = run.out.str();

    const std::string refused = "REFUSED: " + std::string(rrs::kLoginFailedMessage);
    CHECK(occurrences(text, refused) == 2);
    CHECK(text.find("INVALID") == std::string::npos);
    CHECK(text.find(std::string(rrs::menu_title(rrs::MenuTree::Administrator))) ==
          std::string::npos);
    CHECK(run.role_actions.empty());
}

TEST_CASE("TC-AUTH-02: after logout the public menu returns and re-login is required",
          "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));

    // Log in, log out (7), then pick Register (1) from the public menu: the input after logout
    // is read by the public menu, not the passenger menu, so "1" is Register, not Search.
    Session run;
    run_public_menu("2\npassenger1\nPass1Secure\n7\n"
                    "1\npassenger2\nPass2Secure\np2@example.test\n"
                    "2\npassenger1\nWrong1Pass\n"
                    "3\n",
                    users, run);
    const auto text = run.out.str();

    CHECK(text.find("OK: logged out (ref: passenger1)") != std::string::npos);
    const auto logout_at = text.find("OK: logged out");
    const auto public_menu_at =
        text.find(std::string(rrs::menu_title(rrs::MenuTree::Public)), logout_at);
    CHECK(public_menu_at != std::string::npos);
    CHECK(text.find("OK: passenger account created (ref: passenger2)") != std::string::npos);
    CHECK(run.role_actions.empty());
    CHECK(occurrences(text, "OK: logged in") == 1);
    CHECK(users.count() == 3);
}

TEST_CASE("TC-AUTH-02: end of input inside a session ends it without error",
          "[integration][M4][M17]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));

    Session run;
    run_public_menu("2\nadmin\nAdminPassword1\n", users, run);
    CHECK(run.out.str().find("OK: logged in (ref: admin)") != std::string::npos);
    CHECK(run.err.str().empty());
}
