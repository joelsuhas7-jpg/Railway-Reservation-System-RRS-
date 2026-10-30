#include "authentication.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rrs {
namespace {

constexpr char kRoleAdministrator = 'A';
constexpr char kRolePassenger = 'P';
constexpr char kStatusActive = 'A';
constexpr char kStatusLocked = 'L';

ValidationResult failure(std::string_view field, std::string message) {
    return {false, std::string(field), std::move(message)};
}

AuthResult from_validation(const ValidationResult& result) {
    return {AuthStatus::InvalidInput, result.field, result.message};
}

void put_text(std::string& record, std::size_t& offset, std::string_view text,
              std::size_t width) {
    record.replace(offset, text.size(), text);
    offset += width;
}

std::string get_text(std::string_view record, std::size_t& offset, std::size_t width) {
    const auto field = record.substr(offset, width);
    offset += width;
    return std::string(field.substr(0, field.find('\0')));
}

bool equal_ignoring_case(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

}  // namespace

ValidationResult validate_user_id(std::string_view user_id) {
    constexpr std::string_view field = "user id";
    auto result = validate_presence(field, user_id);
    if (!result) {
        return result;
    }
    result = validate_length(field, user_id, kUserIdMinLength, kUserIdMaxLength);
    if (!result) {
        return result;
    }
    for (const unsigned char character : user_id) {
        if (std::isalnum(character) == 0 && character != '_') {
            return failure(field, "may contain only letters, digits and underscore");
        }
    }
    return {};
}

ValidationResult validate_password_policy(std::string_view password) {
    constexpr std::string_view field = "password";
    if (password.size() < kPasswordMinLength) {
        return failure(field, "must be at least " + std::to_string(kPasswordMinLength) +
                                  " characters");
    }
    const bool has_letter = std::any_of(password.begin(), password.end(), [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
    });
    const bool has_digit = std::any_of(password.begin(), password.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
    if (!has_letter || !has_digit) {
        return failure(field, "must include at least one letter and one digit");
    }
    return {};
}

ValidationResult validate_contact(std::string_view contact) {
    constexpr std::string_view field = "contact";
    auto result = validate_presence(field, contact);
    if (!result) {
        return result;
    }
    result = validate_length(field, contact, 1, kContactMaxLength);
    if (!result) {
        return result;
    }
    for (const unsigned char character : contact) {
        if (std::isprint(character) == 0) {
            return failure(field, "may contain only printable characters");
        }
    }
    return {};
}

std::string encode_user(const UserAccount& account) {
    if (account.user_id.size() > kUserIdMaxLength || account.contact.size() > kContactMaxLength) {
        throw std::invalid_argument("user account field exceeds its record width");
    }
    std::string record(kUserRecordSize, '\0');
    std::size_t offset = 0;
    put_text(record, offset, account.user_id, kUserIdMaxLength);
    record[offset++] = account.role == Role::Administrator ? kRoleAdministrator : kRolePassenger;
    record[offset++] = account.status == AccountStatus::Locked ? kStatusLocked : kStatusActive;
    for (const auto byte : account.digest.salt) {
        record[offset++] = static_cast<char>(byte);
    }
    for (const auto byte : account.digest.hash) {
        record[offset++] = static_cast<char>(byte);
    }
    record[offset++] = static_cast<char>(account.failed_attempts);
    auto locked_until = static_cast<std::uint64_t>(account.locked_until);
    for (int i = 0; i < 8; ++i) {
        record[offset++] = static_cast<char>(locked_until & 0xFFU);
        locked_until >>= 8;
    }
    put_text(record, offset, account.contact, kContactMaxLength);
    return record;
}

UserAccount decode_user(std::string_view record) {
    if (record.size() != kUserRecordSize) {
        throw std::runtime_error("corrupt user record: wrong size");
    }
    UserAccount account;
    std::size_t offset = 0;
    account.user_id = get_text(record, offset, kUserIdMaxLength);

    const char role = record[offset++];
    if (role != kRoleAdministrator && role != kRolePassenger) {
        throw std::runtime_error("corrupt user record: unknown role");
    }
    account.role = role == kRoleAdministrator ? Role::Administrator : Role::Passenger;

    const char status = record[offset++];
    if (status != kStatusActive && status != kStatusLocked) {
        throw std::runtime_error("corrupt user record: unknown account status");
    }
    account.status = status == kStatusLocked ? AccountStatus::Locked : AccountStatus::Active;

    for (auto& byte : account.digest.salt) {
        byte = static_cast<std::uint8_t>(record[offset++]);
    }
    for (auto& byte : account.digest.hash) {
        byte = static_cast<std::uint8_t>(record[offset++]);
    }
    account.failed_attempts = static_cast<std::uint8_t>(record[offset++]);
    std::uint64_t locked_until = 0;
    for (int i = 0; i < 8; ++i) {
        locked_until |= static_cast<std::uint64_t>(static_cast<unsigned char>(record[offset++]))
                        << (8 * i);
    }
    account.locked_until = static_cast<std::int64_t>(locked_until);
    account.contact = get_text(record, offset, kContactMaxLength);

    if (!validate_user_id(account.user_id)) {
        throw std::runtime_error("corrupt user record: invalid user id");
    }
    return account;
}

UserStore::UserStore(PersistenceStore store) : store_(std::move(store)) {}

std::size_t UserStore::count() const {
    return store_.record_count(kUsersFile, kUserRecordSize);
}

std::optional<std::size_t> UserStore::find_index(std::string_view user_id) const {
    const auto total = count();
    for (std::size_t index = 0; index < total; ++index) {
        const auto record = store_.read_record(kUsersFile, kUserRecordSize, index);
        if (!record) {
            break;
        }
        if (equal_ignoring_case(decode_user(*record).user_id, user_id)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<UserAccount> UserStore::find(std::string_view user_id) const {
    const auto index = find_index(user_id);
    if (!index) {
        return std::nullopt;
    }
    const auto record = store_.read_record(kUsersFile, kUserRecordSize, *index);
    if (!record) {
        return std::nullopt;
    }
    return decode_user(*record);
}

void UserStore::append(const UserAccount& account) const {
    store_.write_record(kUsersFile, kUserRecordSize, count(), encode_user(account));
}

void UserStore::update(const UserAccount& account) const {
    const auto index = find_index(account.user_id);
    if (!index) {
        throw std::runtime_error("cannot update an account that does not exist");
    }
    store_.write_record(kUsersFile, kUserRecordSize, *index, encode_user(account));
}

bool bootstrap_required(const UserStore& users) {
    return users.count() == 0;
}

AuthResult bootstrap_admin(const UserStore& users, std::string_view user_id,
                           std::string_view password, std::string_view contact) {
    if (!bootstrap_required(users)) {
        return {AuthStatus::Refused, {}, "first-run administrator setup is already complete"};
    }
    if (auto result = validate_user_id(user_id); !result) {
        return from_validation(result);
    }
    if (auto result = validate_password_policy(password); !result) {
        return from_validation(result);
    }
    if (auto result = validate_contact(contact); !result) {
        return from_validation(result);
    }

    UserAccount admin;
    admin.user_id = std::string(user_id);
    admin.role = Role::Administrator;
    admin.digest = hash_password(password);
    admin.contact = std::string(contact);
    users.append(admin);
    return {AuthStatus::Ok, {}, "administrator account created"};
}

AuthResult register_passenger(const UserStore& users, std::string_view user_id,
                              std::string_view password, std::string_view contact) {
    if (bootstrap_required(users)) {
        return {AuthStatus::Refused, {}, "the administrator account must be created first"};
    }
    if (auto result = validate_user_id(user_id); !result) {
        return from_validation(result);
    }
    if (auto result = validate_password_policy(password); !result) {
        return from_validation(result);
    }
    if (auto result = validate_contact(contact); !result) {
        return from_validation(result);
    }
    if (users.find(user_id)) {
        return {AuthStatus::InvalidInput, "user id", "is already registered"};
    }

    UserAccount passenger;
    passenger.user_id = std::string(user_id);
    passenger.role = Role::Passenger;
    passenger.digest = hash_password(password);
    passenger.contact = std::string(contact);
    users.append(passenger);
    return {AuthStatus::Ok, {}, "passenger account created"};
}

LoginResult authenticate(const UserStore& users, std::string_view user_id,
                         std::string_view password) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return authenticate(users, user_id, password, now);
}

LoginResult authenticate(const UserStore& users, std::string_view user_id,
                         std::string_view password, std::int64_t now_epoch_seconds) {
    const LoginResult failed{{AuthStatus::Refused, {}, std::string(kLoginFailedMessage)}, {}};

    auto account = validate_user_id(user_id) ? users.find(user_id) : std::nullopt;
    if (!account) {
        // UC-05 E1: same digest work as a real account, so timing does not reveal the id.
        static const PasswordDigest dummy{};
        static_cast<void>(verify_password(password, dummy));
        return failed;
    }

    if (account->status == AccountStatus::Locked) {
        if (now_epoch_seconds < account->locked_until) {
            const auto remaining = account->locked_until - now_epoch_seconds;
            return {{AuthStatus::Refused, {}, "account locked; try again in " +
                                                   std::to_string(remaining) + " seconds"}, {}};
        }
        account->status = AccountStatus::Active;
        account->failed_attempts = 0;
        account->locked_until = 0;
    }

    if (!verify_password(password, account->digest)) {
        // UC-05 E2. Locking after the fifth failure is RRS-SR-006 (#16).
        if (account->failed_attempts < std::numeric_limits<std::uint8_t>::max()) {
            ++account->failed_attempts;
        }
        if (account->failed_attempts >= 5) {
            account->status = AccountStatus::Locked;
            account->locked_until = now_epoch_seconds + kAccountLockDurationSeconds;
        }
        users.update(*account);
        return failed;
    }

    if (account->failed_attempts != 0 || account->status != AccountStatus::Active ||
        account->locked_until != 0) {
        account->failed_attempts = 0;
        account->status = AccountStatus::Active;
        account->locked_until = 0;
        users.update(*account);
    }
    return {{AuthStatus::Ok, {}, "logged in"}, Session{account->user_id, account->role}};
}

}  // namespace rrs
