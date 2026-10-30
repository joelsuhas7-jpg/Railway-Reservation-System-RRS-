#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "password_digest.hpp"
#include "persistence.hpp"
#include "validation.hpp"

namespace rrs {

// M4 AuthenticationModule: user accounts in users.dat (SRS s4.1, RRS-F-001, RRS-F-004).

inline constexpr std::string_view kUsersFile = "users.dat";

inline constexpr std::size_t kUserIdMinLength = 3;
inline constexpr std::size_t kUserIdMaxLength = 32;
inline constexpr std::size_t kPasswordMinLength = 8;
inline constexpr std::size_t kContactMaxLength = 64;

// users.dat record: user id (32) | role (1) | status (1) | salt (16) | digest (32) |
// failed attempts (1) | locked-until epoch seconds, little-endian (8) | contact (64).
// Text fields are padded with NUL bytes.
inline constexpr std::size_t kUserRecordSize = kUserIdMaxLength + 1 + 1 + kPasswordSaltSize +
                                               kPasswordDigestSize + 1 + 8 + kContactMaxLength;

enum class Role { Administrator, Passenger };
enum class AccountStatus { Active, Locked };

struct UserAccount {
    std::string user_id;
    Role role{Role::Passenger};
    AccountStatus status{AccountStatus::Active};
    PasswordDigest digest;
    std::uint8_t failed_attempts{0};
    std::int64_t locked_until{0};
    std::string contact;
};

// User id: 3-32 characters, letters, digits and underscore only.
ValidationResult validate_user_id(std::string_view user_id);

// RRS-SR-006: at least 8 characters, including at least one letter and one digit.
ValidationResult validate_password_policy(std::string_view password);

// Contact details: 1-64 printable characters.
ValidationResult validate_contact(std::string_view contact);

// Fixed-length record conversion. decode_user throws std::runtime_error on a corrupt record.
std::string encode_user(const UserAccount& account);
UserAccount decode_user(std::string_view record);

// Accounts in users.dat. User ids are unique regardless of letter case.
class UserStore {
public:
    explicit UserStore(PersistenceStore store);

    std::size_t count() const;
    std::optional<UserAccount> find(std::string_view user_id) const;
    void append(const UserAccount& account) const;
    // Rewrites the stored record with the same user id. Throws std::runtime_error if absent.
    void update(const UserAccount& account) const;

private:
    PersistenceStore store_;

    std::optional<std::size_t> find_index(std::string_view user_id) const;
};

enum class AuthStatus { Ok, InvalidInput, Refused };

// field names the offending input for InvalidInput; empty otherwise.
struct AuthResult {
    AuthStatus status{AuthStatus::Ok};
    std::string field;
    std::string message;

    explicit operator bool() const noexcept { return status == AuthStatus::Ok; }
};

// RRS-F-004: true only while users.dat holds no account. While it is true, administrator
// creation is the only operation the application offers.
bool bootstrap_required(const UserStore& users);

// RRS-F-004: creates the single first administrator. Refused once any account exists, so the
// bootstrap path closes permanently after it succeeds.
AuthResult bootstrap_admin(const UserStore& users, std::string_view user_id,
                           std::string_view password, std::string_view contact);

// RRS-F-001: creates one PASSENGER account. Refused until the administrator bootstrap is
// done; a duplicate user id (any letter case) is rejected naming the "user id" field.
AuthResult register_passenger(const UserStore& users, std::string_view user_id,
                              std::string_view password, std::string_view contact);

// In-memory session record bound to the account's role (RRS-F-002). Logging out discards it
// (RRS-F-003).
struct Session {
    std::string user_id;
    Role role{Role::Passenger};
};

inline constexpr std::string_view kLoginFailedMessage = "user id or password is incorrect";
inline constexpr std::uint8_t kMaxFailedAttempts = 5;
inline constexpr std::int64_t kAccountLockDurationSeconds = 15 * 60;

struct LoginResult {
    AuthResult result;
    std::optional<Session> session;
};

// RRS-F-002, UC-05: on success opens a session and resets the failed-attempt counter. Five
// consecutive wrong passwords lock the account for 15 minutes. An unknown id costs one dummy
// digest of equal work. Credential failures are generic; an active lock reports its remaining
// duration as required by UC-05.
LoginResult authenticate(const UserStore& users, std::string_view user_id,
                         std::string_view password);
// Time-injectable overload for deterministic lockout decisions and boundary tests.
LoginResult authenticate(const UserStore& users, std::string_view user_id,
                         std::string_view password, std::int64_t now_epoch_seconds);

}  // namespace rrs
