#include "passenger.hpp"


#include <string>

namespace rrs {

ValidationResult validate_passenger(const Passenger& passenger) {
    auto name_presence = validate_presence("name", passenger.name);
    if (!name_presence) {
        return name_presence;
    }

    auto name_length = validate_length("name", passenger.name, 1, 60);
    if (!name_length) {
        return name_length;
    }

    auto age_range = validate_range("age", passenger.age, 1, 120);
    if (!age_range) {
        return age_range;
    }

    switch (passenger.gender) {
        case Gender::Male:
        case Gender::Female:
        case Gender::Other:
            return {};

        default:
            return {false, "gender", "must be Male, Female, or Other"};
    }
}

ValidationResult validate_passengers(const std::vector<Passenger>& passengers) {
    if (passengers.size() < kMinPassengers ||
        passengers.size() > kMaxPassengers) {
        return {false, "passengers", "must contain between 1 and 6 passengers"};
    }

    for (std::size_t i = 0; i < passengers.size(); ++i) {
        auto result = validate_passenger(passengers[i]);
        if (!result) {
            result.field += " (passenger " + std::to_string(i + 1) + ")";
            return result;
        }
    }

    return {};
}

}  // namespace rrs

