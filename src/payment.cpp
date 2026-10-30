#include "payment.hpp"

#include <stdexcept>
#include <string>

namespace rrs {

PaymentRecord PaymentModule::record_outcome(
    PaymentOutcome outcome,
    std::string_view reference) const {

    if (outcome != PaymentOutcome::success &&
        outcome != PaymentOutcome::failure) {
        throw std::invalid_argument("invalid payment outcome");
    }

    if (reference.empty()) {
        throw std::invalid_argument("payment reference must not be empty");
    }

    return PaymentRecord{outcome, std::string(reference)};
}

}  // namespace rrs
