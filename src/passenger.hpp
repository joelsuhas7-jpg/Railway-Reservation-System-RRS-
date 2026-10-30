#pragma once

#include <string>
#include <vector>

#include "validation.hpp"

namespace rrs {

enum class Gender {
    Male,
    Female,
    Other
};

struct Passenger {
    std::string name;
    int age{};
    Gender gender{Gender::Other};
};

constexpr std::size_t kMinPassengers = 1;
constexpr std::size_t kMaxPassengers = 6;

// Validates one passenger and returns the first validation error, if any.
ValidationResult validate_passenger(const Passenger& passenger);

// Validates the passenger list count and each passenger's details.
ValidationResult validate_passengers(const std::vector<Passenger>& passengers);

}  // namespace rrs
