#include "console_input.hpp"

#include <charconv>
#include <istream>
#include <ostream>
#include <system_error>

namespace rrs {

bool read_field(std::istream& in, std::ostream& out, const char* label, std::string& value) {
    out << label << ": " << std::flush;
    if (!std::getline(in, value)) {
        out << '\n';
        return false;
    }
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return true;
}

bool parse_menu_number(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parse_train_number(std::string_view text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0;
}

Outcome read_train_number(std::istream& in, std::ostream& out, std::uint32_t& train_number,
                          bool& end_of_input, const SessionActivityCheck& session_activity) {
    std::string value;
    if (session_activity && !session_activity(false)) {
        end_of_input = true;
        return Outcome::refusal(std::string(kSessionExpiredMessage));
    }
    if (!read_field(in, out, "Train number", value)) {
        end_of_input = true;
        return Outcome::refusal("train operation cancelled");
    }
    if (session_activity && !session_activity(true)) {
        end_of_input = true;
        return Outcome::refusal(std::string(kSessionExpiredMessage));
    }
    if (!parse_train_number(value, train_number)) {
        return Outcome::validation_failure("train number", "enter a nonzero 32-bit integer");
    }
    return Outcome::success("train number read");
}

}  // namespace rrs
