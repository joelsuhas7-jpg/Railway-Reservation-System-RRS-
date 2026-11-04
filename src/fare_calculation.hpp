#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "fare_config.hpp"

namespace rrs {

// RRS-F-014: configured fare x passenger count in int64 paise. nullopt for a non-positive fare,
// zero passengers or an overflowing product. No other fare derivation exists (C-006).
std::optional<std::int64_t> total_fare_paise(std::int64_t fare_paise,
                                             std::size_t passenger_count) noexcept;

struct FareQuote {
    FareLookupState state{FareLookupState::NoApplicableFare};
    std::int64_t fare_paise{0};
    std::size_t passenger_count{0};
    std::int64_t total_paise{0};
    std::string message;  // why no total is available; empty when found

    bool found() const noexcept { return state == FareLookupState::Found; }
};

// M11 FareCalculationModule: reads the fare M7 configured and never writes or invents one.
class FareCalculationModule final {
public:
    explicit FareCalculationModule(const FareConfigModule& fares);

    // A quote without a configured fare carries no total, so the booking must be refused.
    FareQuote quote(std::uint32_t train_number, std::string_view class_code,
                    std::string_view journey_date, std::size_t passenger_count) const;

private:
    const FareConfigModule& fares_;
};

}  // namespace rrs
