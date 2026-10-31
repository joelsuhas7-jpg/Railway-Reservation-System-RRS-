#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// Shared line-input helpers for the M17 console flows.

inline constexpr std::string_view kSessionExpiredMessage =
    "session expired after 15 minutes of inactivity; please log in again";

// Prompts "label: " and reads one line without its trailing CR. False at end of input.
bool read_field(std::istream& in, std::ostream& out, const char* label, std::string& value);

// Parses a whole decimal string. False for empty text, trailing characters or overflow.
bool parse_menu_number(std::string_view text, int& value);
bool parse_train_number(std::string_view text, std::uint32_t& value);  // also rejects zero

// Reads a train number between session-activity checks. Sets end_of_input when input ends or
// the session expires; otherwise returns a validation failure or success.
Outcome read_train_number(std::istream& in, std::ostream& out, std::uint32_t& train_number,
                          bool& end_of_input, const SessionActivityCheck& session_activity = {});

}  // namespace rrs
