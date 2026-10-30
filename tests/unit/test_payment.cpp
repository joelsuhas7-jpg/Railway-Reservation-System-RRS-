#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "payment.hpp"

TEST_CASE("TC-PAY-01: successful payment outcome and reference are recorded",
          "[unit][M12]") {
    rrs::PaymentModule payment;

    const auto record =
        payment.record_outcome(rrs::PaymentOutcome::success, "PAY123456");

    CHECK(record.outcome == rrs::PaymentOutcome::success);
    CHECK(record.reference == "PAY123456");
}

TEST_CASE("TC-PAY-01: failed payment outcome and reference are recorded",
          "[unit][M12]") {
    rrs::PaymentModule payment;

    const auto record =
        payment.record_outcome(rrs::PaymentOutcome::failure, "PAY654321");

    CHECK(record.outcome == rrs::PaymentOutcome::failure);
    CHECK(record.reference == "PAY654321");
}

TEST_CASE("TC-PAY-01: empty payment reference is rejected",
          "[unit][M12]") {
    rrs::PaymentModule payment;

    CHECK_THROWS_AS(
        payment.record_outcome(rrs::PaymentOutcome::success, ""),
        std::invalid_argument);

    CHECK_THROWS_AS(
        payment.record_outcome(rrs::PaymentOutcome::failure, ""),
        std::invalid_argument);
}

TEST_CASE("TC-PAY-01: invalid payment outcome is rejected",
          "[unit][M12]") {
    rrs::PaymentModule payment;

    const auto invalid_outcome =
        static_cast<rrs::PaymentOutcome>(99);

    CHECK_THROWS_AS(
        payment.record_outcome(invalid_outcome, "PAY123456"),
        std::invalid_argument);
}
