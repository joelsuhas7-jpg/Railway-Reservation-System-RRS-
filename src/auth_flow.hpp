#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "authentication.hpp"
#include "console_ui.hpp"

namespace rrs {

// Console flows that connect M17 prompts to M4 account operations.

struct AccountForm {
    std::string user_id;
    std::string password;
    std::string contact;
};

// Prompts for user id, password and contact, one line each. nullopt at end of input.
std::optional<AccountForm> read_account_form(std::istream& in, std::ostream& out);

// Maps an M4 result to one of the four M17 outcomes; record_id is shown on success.
Outcome to_outcome(const AuthResult& result, const std::string& record_id);

// RRS-F-004: repeats the administrator prompts until the bootstrap succeeds. Returns false if
// input ends first. Returns true at once when an account already exists. I/O errors throw.
bool run_bootstrap(std::istream& in, std::ostream& out, std::ostream& err,
                   const UserStore& users);

// RRS-F-001: the public menu's Register action.
Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users);

}  // namespace rrs
