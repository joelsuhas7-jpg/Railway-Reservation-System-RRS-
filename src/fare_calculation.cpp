#include "fare_calculation.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "passenger.hpp"

namespace rrs {

std::optional<std::int64_t> total_fare_paise(std::int64_t fare_paise,
                                             std::size_t passenger_count) noexcept {
    if (fare_paise <= 0 || passenger_count == 0 ||
        passenger_count > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto count = static_cast<std::int64_t>(passenger_count);
    if (fare_paise > std::numeric_limits<std::int64_t>::max() / count) {
        return std::nullopt;
    }
    return fare_paise * count;
}

FareCalculationModule::FareCalculationModule(const FareConfigModule& fares) : fares_(fares) {}

FareQuote FareCalculationModule::quote(std::uint32_t train_number, std::string_view class_code,
                                       std::string_view journey_date,
                                       std::size_t passenger_count) const {
    FareQuote result;
    result.passenger_count = passenger_count;
    if (passenger_count < kMinPassengers || passenger_count > kMaxPassengers) {
        result.message = "passenger count must be between 1 and 6";
        return result;
    }
    const auto lookup = fares_.find_applicable_fare(train_number, class_code, journey_date);
    result.state = lookup.state;
    if (lookup.state != FareLookupState::Found || !lookup.record) {
        result.state = lookup.state == FareLookupState::Found ? FareLookupState::NoApplicableFare
                                                              : lookup.state;
        result.message =
            lookup.message.empty() ? std::string(kFareUnavailableMessage) : lookup.message;
        return result;
    }
    const auto total = total_fare_paise(lookup.record->fare_paise, passenger_count);
    if (!total) {
        result.state = FareLookupState::NoApplicableFare;
        result.message = "total fare is out of range";
        return result;
    }
    result.fare_paise = lookup.record->fare_paise;
    result.total_paise = *total;
    return result;
}

}  // namespace rrs
