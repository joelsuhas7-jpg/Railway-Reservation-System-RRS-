#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rrs {

// Result from validating one input field. A valid result has no field or message.
struct ValidationResult {
    bool valid{true};
    std::string field;
    std::string message;

    explicit operator bool() const noexcept { return valid; }
};

// Rejects empty and whitespace-only values.
ValidationResult validate_presence(std::string_view field, std::string_view value);

// Checks an inclusive byte-length range.
ValidationResult validate_length(std::string_view field, std::string_view value,
                                 std::size_t minimum, std::size_t maximum);

// Checks that the complete value is a base-10 signed integer. No whitespace or
// trailing characters are accepted.
ValidationResult validate_integer_type(std::string_view field, std::string_view value);

// Checks an inclusive numeric range.
ValidationResult validate_range(std::string_view field, std::int64_t value,
                                std::int64_t minimum, std::int64_t maximum);

// Combines presence, integer type and range checks for numeric text fields.
ValidationResult validate_integer(std::string_view field, std::string_view value,
                                  std::int64_t minimum, std::int64_t maximum);

}  // namespace rrs
