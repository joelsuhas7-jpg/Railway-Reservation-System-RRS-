#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

#include "authentication.hpp"
#include "fixtures.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-auth-test-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create auth test directory",
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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

constexpr std::string_view kContact = "admin@example.test";

}  // namespace

TEST_CASE("TC-AUTH-03: empty data directory requires the administrator bootstrap",
          "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    CHECK(users.count() == 0);
    CHECK(rrs::bootstrap_required(users));
}

TEST_CASE("TC-AUTH-03: bootstrap creates exactly one ADMIN account and closes", "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};

    const auto result = rrs::bootstrap_admin(users, admin.username, admin.password, kContact);
    REQUIRE(result);
    CHECK(users.count() == 1);
    CHECK_FALSE(rrs::bootstrap_required(users));

    const auto stored = users.find(admin.username);
    REQUIRE(stored.has_value());
    CHECK(stored->role == rrs::Role::Administrator);
    CHECK(stored->status == rrs::AccountStatus::Active);
    CHECK(stored->contact == kContact);
    CHECK(rrs::verify_password(admin.password, stored->digest));
}

TEST_CASE("TC-AUTH-03: a second bootstrap is refused, also after reopening the store",
          "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    {
        const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
        REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));
        const auto again = rrs::bootstrap_admin(users, "second_admin", "Another1Pass", kContact);
        CHECK(again.status == rrs::AuthStatus::Refused);
    }
    const rrs::UserStore reopened{rrs::PersistenceStore{dir.path()}};
    const auto later = rrs::bootstrap_admin(reopened, "third_admin", "Another1Pass", kContact);
    CHECK(later.status == rrs::AuthStatus::Refused);
    CHECK(reopened.count() == 1);
    CHECK_FALSE(reopened.find("second_admin").has_value());
}

TEST_CASE("TC-AUTH-03: invalid bootstrap input names the field and keeps the path open",
          "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};

    const auto bad_id = rrs::bootstrap_admin(users, "ad", "AdminPassword1", kContact);
    CHECK(bad_id.status == rrs::AuthStatus::InvalidInput);
    CHECK(bad_id.field == "user id");

    const auto bad_password = rrs::bootstrap_admin(users, "admin", "Admin12", kContact);
    CHECK(bad_password.status == rrs::AuthStatus::InvalidInput);
    CHECK(bad_password.field == "password");

    const auto bad_contact = rrs::bootstrap_admin(users, "admin", "AdminPassword1", "");
    CHECK(bad_contact.status == rrs::AuthStatus::InvalidInput);
    CHECK(bad_contact.field == "contact");

    CHECK(users.count() == 0);
    CHECK(rrs::bootstrap_required(users));
}

TEST_CASE("TC-SEC-04: users.dat never contains the plaintext password", "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));

    const auto raw = read_file(dir.path() / std::string(rrs::kUsersFile));
    CHECK(raw.size() == rrs::kUserRecordSize);
    CHECK(raw.find(admin.password) == std::string::npos);
}

TEST_CASE("TC-SEC-06: password policy needs 8 characters, a letter and a digit", "[unit][M4]") {
    CHECK_FALSE(rrs::validate_password_policy("Abcdef1"));   // 7 characters
    CHECK(rrs::validate_password_policy("Abcdefg1"));        // 8 characters
    CHECK_FALSE(rrs::validate_password_policy("Abcdefgh"));  // no digit
    CHECK_FALSE(rrs::validate_password_policy("12345678"));  // no letter
    CHECK_FALSE(rrs::validate_password_policy(""));
    CHECK(rrs::validate_password_policy("Abcdef1").field == "password");
}

TEST_CASE("RRS-F-004-01: user id length and character boundaries", "[unit][M4]") {
    CHECK_FALSE(rrs::validate_user_id(""));
    CHECK_FALSE(rrs::validate_user_id("ab"));
    CHECK(rrs::validate_user_id("abc"));
    CHECK(rrs::validate_user_id(std::string(32, 'a')));
    CHECK_FALSE(rrs::validate_user_id(std::string(33, 'a')));
    CHECK(rrs::validate_user_id("passenger_lockout"));
    CHECK_FALSE(rrs::validate_user_id("bad id"));
    CHECK_FALSE(rrs::validate_user_id("bad-id"));
    CHECK(rrs::validate_user_id("ab").field == "user id");
}

TEST_CASE("RRS-F-004-02: contact length boundaries", "[unit][M4]") {
    CHECK_FALSE(rrs::validate_contact(""));
    CHECK_FALSE(rrs::validate_contact("   "));
    CHECK(rrs::validate_contact("x"));
    CHECK(rrs::validate_contact(std::string(64, 'x')));
    CHECK_FALSE(rrs::validate_contact(std::string(65, 'x')));
    CHECK_FALSE(rrs::validate_contact("line\nbreak"));
}

TEST_CASE("RRS-F-004-03: user record round-trips and rejects corruption", "[unit][M4]") {
    rrs::UserAccount account;
    account.user_id = "passenger1";
    account.role = rrs::Role::Passenger;
    account.status = rrs::AccountStatus::Locked;
    account.digest = rrs::hash_password("Pass1Secure");
    account.failed_attempts = 5;
    account.locked_until = 1793000000;
    account.contact = std::string(64, 'c');

    const auto record = rrs::encode_user(account);
    REQUIRE(record.size() == rrs::kUserRecordSize);
    const auto decoded = rrs::decode_user(record);
    CHECK(decoded.user_id == account.user_id);
    CHECK(decoded.role == account.role);
    CHECK(decoded.status == account.status);
    CHECK(decoded.digest.salt == account.digest.salt);
    CHECK(decoded.digest.hash == account.digest.hash);
    CHECK(decoded.failed_attempts == 5);
    CHECK(decoded.locked_until == 1793000000);
    CHECK(decoded.contact == account.contact);

    auto bad_role = record;
    bad_role[rrs::kUserIdMaxLength] = 'X';
    CHECK_THROWS_AS(rrs::decode_user(bad_role), std::runtime_error);
    CHECK_THROWS_AS(rrs::decode_user(record.substr(1)), std::runtime_error);
}

TEST_CASE("RRS-F-004-04: user lookup ignores letter case", "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));
    CHECK(users.find("ADMIN").has_value());
    CHECK_FALSE(users.find("nobody").has_value());
}

TEST_CASE("TC-REG-01: valid details create exactly one PASSENGER account", "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const auto& passenger = rrs::known_passenger_accounts()[0];
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));

    const auto result =
        rrs::register_passenger(users, passenger.username, passenger.password, "p1@example.test");
    REQUIRE(result);
    CHECK(users.count() == 2);

    const auto stored = users.find(passenger.username);
    REQUIRE(stored.has_value());
    CHECK(stored->role == rrs::Role::Passenger);
    CHECK(stored->status == rrs::AccountStatus::Active);
    CHECK(stored->failed_attempts == 0);
    CHECK(stored->contact == "p1@example.test");
    CHECK(rrs::verify_password(passenger.password, stored->digest));
}

TEST_CASE("TC-REG-01: a duplicate user id is rejected naming the field", "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));

    const auto same = rrs::register_passenger(users, "passenger1", "Other1Pass", "x@example.test");
    CHECK(same.status == rrs::AuthStatus::InvalidInput);
    CHECK(same.field == "user id");

    const auto other_case =
        rrs::register_passenger(users, "PASSENGER1", "Other1Pass", "x@example.test");
    CHECK(other_case.status == rrs::AuthStatus::InvalidInput);
    CHECK(other_case.field == "user id");

    const auto clashes_with_admin =
        rrs::register_passenger(users, admin.username, "Other1Pass", "x@example.test");
    CHECK(clashes_with_admin.field == "user id");
    CHECK(users.count() == 2);
}

TEST_CASE("TC-REG-01: registration is refused before the administrator bootstrap",
          "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    const auto result = rrs::register_passenger(users, "passenger1", "Pass1Secure", "p@x.test");
    CHECK(result.status == rrs::AuthStatus::Refused);
    CHECK(users.count() == 0);
    CHECK(rrs::bootstrap_required(users));
}

TEST_CASE("TC-REG-01: invalid registration input names the field and creates nothing",
          "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));

    CHECK(rrs::register_passenger(users, "p!", "Pass1Secure", "p@x.test").field == "user id");
    CHECK(rrs::register_passenger(users, "passenger1", "Pass1Sc", "p@x.test").field ==
          "password");
    CHECK(rrs::register_passenger(users, "passenger1", "PassSecure", "p@x.test").field ==
          "password");
    CHECK(rrs::register_passenger(users, "passenger1", "Pass1Secure", "").field == "contact");
    CHECK(users.count() == 1);
}

TEST_CASE("TC-SEC-04: two accounts with the same password get distinct salts", "[unit][M4]") {
    TempDirectory dir;
    const auto& admin = rrs::known_admin_account();
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Same1Password", "a@x.test"));
    REQUIRE(rrs::register_passenger(users, "passenger2", "Same1Password", "b@x.test"));

    const auto first = users.find("passenger1");
    const auto second = users.find("passenger2");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->digest.salt != second->digest.salt);
    CHECK(first->digest.hash != second->digest.hash);

    const auto raw = read_file(dir.path() / std::string(rrs::kUsersFile));
    CHECK(raw.find("Same1Password") == std::string::npos);
}

namespace {

// Bootstraps the seed administrator and registers passenger1 (Pass1Secure).
void seed_two_accounts(const rrs::UserStore& users) {
    const auto& admin = rrs::known_admin_account();
    REQUIRE(rrs::bootstrap_admin(users, admin.username, admin.password, kContact));
    REQUIRE(rrs::register_passenger(users, "passenger1", "Pass1Secure", "p1@example.test"));
}

}  // namespace

TEST_CASE("TC-AUTH-01: correct credentials open a session bound to the account's role",
          "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    seed_two_accounts(users);
    const auto& admin = rrs::known_admin_account();

    const auto as_admin = rrs::authenticate(users, admin.username, admin.password);
    REQUIRE(as_admin.result);
    REQUIRE(as_admin.session.has_value());
    CHECK(as_admin.session->user_id == admin.username);
    CHECK(as_admin.session->role == rrs::Role::Administrator);

    const auto as_passenger = rrs::authenticate(users, "passenger1", "Pass1Secure");
    REQUIRE(as_passenger.session.has_value());
    CHECK(as_passenger.session->role == rrs::Role::Passenger);
}

TEST_CASE("TC-AUTH-01: wrong password and unknown id fail with the same message",
          "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    seed_two_accounts(users);

    const auto wrong_password = rrs::authenticate(users, "passenger1", "Wrong1Pass");
    const auto unknown_id = rrs::authenticate(users, "nobody", "Pass1Secure");
    const auto malformed_id = rrs::authenticate(users, "no body!", "Pass1Secure");
    const auto empty = rrs::authenticate(users, "", "");

    for (const auto* login : {&wrong_password, &unknown_id, &malformed_id, &empty}) {
        CHECK_FALSE(login->session.has_value());
        CHECK(login->result.status == rrs::AuthStatus::Refused);
        CHECK(login->result.field.empty());
        CHECK(login->result.message == rrs::kLoginFailedMessage);
    }
}

TEST_CASE("TC-AUTH-01: failures count up and a successful login resets the counter",
          "[unit][M4]") {
    TempDirectory dir;
    {
        const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
        seed_two_accounts(users);
        CHECK_FALSE(rrs::authenticate(users, "passenger1", "Wrong1Pass").session);
        CHECK_FALSE(rrs::authenticate(users, "PASSENGER1", "Wrong2Pass").session);
    }
    const rrs::UserStore reopened{rrs::PersistenceStore{dir.path()}};
    CHECK(reopened.find("passenger1")->failed_attempts == 2);
    CHECK(reopened.find("admin")->failed_attempts == 0);

    REQUIRE(rrs::authenticate(reopened, "passenger1", "Pass1Secure").session);
    CHECK(reopened.find("passenger1")->failed_attempts == 0);
    CHECK(reopened.count() == 2);
}

TEST_CASE("TC-AUTH-01: an unknown id costs as much digest work as a real account",
          "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    seed_two_accounts(users);

    const auto time_login = [&](std::string_view user_id) {
        const auto start = std::chrono::steady_clock::now();
        static_cast<void>(rrs::authenticate(users, user_id, "Wrong1Pass"));
        return std::chrono::steady_clock::now() - start;
    };
    auto known = std::chrono::steady_clock::duration::max();
    auto unknown = std::chrono::steady_clock::duration::max();
    for (int run = 0; run < 3; ++run) {
        known = std::min(known, time_login("passenger1"));
        unknown = std::min(unknown, time_login("nobody"));
    }
    // Without the dummy digest an unknown id returns about 100 times faster; allow wide noise.
    CHECK(unknown * 4 > known);
}

TEST_CASE("RRS-F-002-01: update rewrites the matching record in place", "[unit][M4]") {
    TempDirectory dir;
    const rrs::UserStore users{rrs::PersistenceStore{dir.path()}};
    seed_two_accounts(users);

    auto account = *users.find("passenger1");
    account.contact = "new@example.test";
    users.update(account);
    CHECK(users.count() == 2);
    CHECK(users.find("passenger1")->contact == "new@example.test");

    rrs::UserAccount missing = account;
    missing.user_id = "ghost";
    CHECK_THROWS_AS(users.update(missing), std::runtime_error);
}
