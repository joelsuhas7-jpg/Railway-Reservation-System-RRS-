#include "validation.hpp"

#include <charconv>
#include <cctype>
#include <string>
#include <utility>

namespace rrs {
namespace {

ValidationResult failure(std::string_view field, std::string message) {
    return {false, std::string(field), std::move(message)};
}

bool parse_integer(std::string_view value, std::int64_t& parsed) {
    if (value.empty()) {
        return false;
    }
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

}  // namespace

ValidationResult validate_presence(std::string_view field, std::string_view value) {
    for (const unsigned char character : value) {
        if (std::isspace(character) == 0) {
            return {};
        }
    }
    return failure(field, "must be provided");
}

ValidationResult validate_length(std::string_view field, std::string_view value,
                                 std::size_t minimum, std::size_t maximum) {
    if (minimum > maximum) {
        return failure(field, "has an invalid length constraint");
    }
    if (value.size() < minimum || value.size() > maximum) {
        return failure(field, "length must be between " + std::to_string(minimum) + " and " +
                                   std::to_string(maximum));
    }
    return {};
}

ValidationResult validate_integer_type(std::string_view field, std::string_view value) {
    std::int64_t parsed = 0;
    if (!parse_integer(value, parsed)) {
        return failure(field, "must be an integer");
    }
    return {};
}

ValidationResult validate_range(std::string_view field, std::int64_t value,
                                std::int64_t minimum, std::int64_t maximum) {
    if (minimum > maximum) {
        return failure(field, "has an invalid range constraint");
    }
    if (value < minimum || value > maximum) {
        return failure(field, "must be between " + std::to_string(minimum) + " and " +
                                   std::to_string(maximum));
    }
    return {};
}

ValidationResult validate_integer(std::string_view field, std::string_view value,
                                  std::int64_t minimum, std::int64_t maximum) {
    auto presence = validate_presence(field, value);
    if (!presence) {
        return presence;
    }

    std::int64_t parsed = 0;
    if (!parse_integer(value, parsed)) {
        return failure(field, "must be an integer");
    }
    return validate_range(field, parsed, minimum, maximum);
}

}  // namespace rrs
