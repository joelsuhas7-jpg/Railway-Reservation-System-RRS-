#pragma once

#include <string>
#include <string_view>

namespace rrs {

enum class PaymentOutcome {
    success,
    failure
};

// Validated payment information to be stored with its pending booking.
// M13 is responsible for persisting the booking and changing its state.
struct PaymentRecord {
    PaymentOutcome outcome;
    std::string reference;
};

class PaymentModule final {
public:
    PaymentRecord record_outcome(
        PaymentOutcome outcome,
        std::string_view reference) const;
};

}  // namespace rrs
