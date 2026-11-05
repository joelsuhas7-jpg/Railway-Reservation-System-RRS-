#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cancellation_refund.hpp"

namespace {
constexpr std::int64_t kHour = 3600;
}  // namespace

TEST_CASE("TC-CAN-01: refund slabs follow Table 4.5-1 at each boundary", "[unit][M15]") {
    using rrs::RefundSlab;
    using rrs::select_refund_slab;
    CHECK(select_refund_slab(50 * kHour) == RefundSlab::S1);
    CHECK(select_refund_slab(48 * kHour) == RefundSlab::S1);
    CHECK(select_refund_slab(48 * kHour - 1) == RefundSlab::S2);
    CHECK(select_refund_slab(12 * kHour) == RefundSlab::S2);
    CHECK(select_refund_slab(12 * kHour - 1) == RefundSlab::S3);
    CHECK(select_refund_slab(4 * kHour) == RefundSlab::S3);
    CHECK(select_refund_slab(4 * kHour - 1) == RefundSlab::S4);
    CHECK(select_refund_slab(3 * kHour) == RefundSlab::S4);
    CHECK(select_refund_slab(0) == RefundSlab::S4);
    CHECK(select_refund_slab(-kHour) == RefundSlab::S4);
}

TEST_CASE("TC-CAN-02: the refund is the slab percentage of the fare in truncated paise",
          "[unit][M15]") {
    using rrs::RefundSlab;
    CHECK(rrs::refund_paise(180000, RefundSlab::S2) == 90000);
    CHECK(rrs::refund_paise(180000, RefundSlab::S1) == 162000);
    CHECK(rrs::refund_paise(180000, RefundSlab::S3) == 45000);
    CHECK(rrs::refund_paise(180000, RefundSlab::S4) == 0);
    CHECK(rrs::refund_paise(45001, RefundSlab::S1) == 40500);  // 40500.9 truncates
    CHECK(rrs::refund_paise(3, RefundSlab::S3) == 0);          // 0.75 truncates
    CHECK(rrs::refund_paise(INT64_MAX, RefundSlab::S1) > 0);   // no overflow
}

TEST_CASE("TC-CAN-01: the interval to departure is counted across month and year ends",
          "[unit][M15]") {
    CHECK(rrs::seconds_until_departure("2026-10-31T04:00:00Z", "2026-11-02", "06:00") ==
          50 * kHour);
    CHECK(rrs::seconds_until_departure("2026-12-31T23:59:59Z", "2027-01-01", "00:00") == 1);
    CHECK(rrs::seconds_until_departure("2028-02-28T06:00:00Z", "2028-03-01", "06:00") ==
          48 * kHour);  // leap day
    CHECK(rrs::seconds_until_departure("2026-11-02T07:00:00Z", "2026-11-02", "06:00") == -kHour);
    CHECK_FALSE(rrs::seconds_until_departure("2026-11-02 07:00", "2026-11-02", "06:00"));
    CHECK_FALSE(rrs::seconds_until_departure("2026-11-02T07:00:00Z", "2026-02-30", "06:00"));
    CHECK_FALSE(rrs::seconds_until_departure("2026-11-02T07:00:00Z", "2026-11-02", "25:00"));
}

TEST_CASE("TC-CAN-03: a cancellation record survives the encoding and reports its status",
          "[unit][M15]") {
    rrs::CancellationRecord record{"A1B2C3D4E5", "2026-11-01T10:00:00Z", rrs::RefundSlab::S2,
                                   90000, {}};
    CHECK(record.status() == rrs::RefundStatus::Pending);
    auto decoded = rrs::decode_cancellation(rrs::encode_cancellation(record));
    CHECK(decoded.pnr == "A1B2C3D4E5");
    CHECK(decoded.cancelled_at == "2026-11-01T10:00:00Z");
    CHECK(decoded.slab == rrs::RefundSlab::S2);
    CHECK(decoded.refund_paise == 90000);
    CHECK(decoded.status() == rrs::RefundStatus::Pending);

    record.disbursement_reference = "DISB-0001";
    decoded = rrs::decode_cancellation(rrs::encode_cancellation(record));
    CHECK(decoded.status() == rrs::RefundStatus::Settled);
    CHECK(std::string(rrs::refund_status_name(decoded.status())) == "SETTLED");

    auto bytes = rrs::encode_cancellation(record);
    bytes.back() = 'X';
    CHECK_THROWS_AS(rrs::decode_cancellation(bytes), std::runtime_error);
    record.slab = rrs::RefundSlab::S4;
    CHECK_THROWS_AS(rrs::encode_cancellation(record), std::invalid_argument);
}
