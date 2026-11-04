#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "booking.hpp"

namespace {

rrs::BookingRecord sample_booking() {
    rrs::BookingRecord record;
    record.pnr = "A1B2C3D4E5";
    record.user_id = "passenger1";
    record.train_number = 12345;
    record.class_code = "SL";
    record.journey_date = "2026-11-02";
    record.passengers = {{"Asha Rao", 34, rrs::Gender::Female},
                         {std::string(60, 'N'), 120, rrs::Gender::Male}};
    record.seat_numbers = {1, 72};
    record.total_fare_paise = 90000;
    record.payment_reference = "PAY-0001";
    record.status = rrs::BookingStatus::Confirmed;
    return record;
}

}  // namespace

TEST_CASE("ENB-07: only the SRS 7.3 booking transitions are legal", "[unit][M13]") {
    using rrs::BookingStatus;
    using rrs::can_transition;
    CHECK(can_transition(BookingStatus::Pending, BookingStatus::Confirmed));
    CHECK(can_transition(BookingStatus::Pending, BookingStatus::Rejected));
    CHECK(can_transition(BookingStatus::Confirmed, BookingStatus::Cancelled));

    CHECK_FALSE(can_transition(BookingStatus::Pending, BookingStatus::Cancelled));
    CHECK_FALSE(can_transition(BookingStatus::Confirmed, BookingStatus::Rejected));
    CHECK_FALSE(can_transition(BookingStatus::Confirmed, BookingStatus::Pending));
    for (const auto to : {BookingStatus::Pending, BookingStatus::Confirmed,
                          BookingStatus::Rejected, BookingStatus::Cancelled}) {
        CHECK_FALSE(can_transition(BookingStatus::Rejected, to));
        CHECK_FALSE(can_transition(BookingStatus::Cancelled, to));
    }
    CHECK_FALSE(can_transition(BookingStatus::Confirmed, BookingStatus::Confirmed));
}

TEST_CASE("ENB-07: a booking record survives the fixed-length encoding", "[unit][M13]") {
    const auto original = sample_booking();
    const auto bytes = rrs::encode_booking(original);
    REQUIRE(bytes.size() == rrs::kBookingRecordSize);

    const auto decoded = rrs::decode_booking(bytes);
    CHECK(decoded.pnr == original.pnr);
    CHECK(decoded.user_id == original.user_id);
    CHECK(decoded.train_number == original.train_number);
    CHECK(decoded.class_code == original.class_code);
    CHECK(decoded.journey_date == original.journey_date);
    REQUIRE(decoded.passengers.size() == 2);
    CHECK(decoded.passengers[1].name == original.passengers[1].name);
    CHECK(decoded.passengers[1].age == 120);
    CHECK(decoded.passengers[0].gender == rrs::Gender::Female);
    CHECK(decoded.seat_numbers == original.seat_numbers);
    CHECK(decoded.total_fare_paise == 90000);
    CHECK(decoded.payment_reference == "PAY-0001");
    CHECK(decoded.status == rrs::BookingStatus::Confirmed);
}

TEST_CASE("ENB-07: oversized fields and corrupt records are refused", "[unit][M13]") {
    auto record = sample_booking();
    record.passengers[0].name = std::string(61, 'N');
    CHECK_THROWS_AS(rrs::encode_booking(record), std::invalid_argument);

    record = sample_booking();
    record.seat_numbers.pop_back();
    CHECK_THROWS_AS(rrs::encode_booking(record), std::invalid_argument);

    auto bytes = rrs::encode_booking(sample_booking());
    bytes.back() = 'X';
    CHECK_THROWS_AS(rrs::decode_booking(bytes), std::runtime_error);
    CHECK_THROWS_AS(rrs::decode_booking(std::string_view(bytes).substr(1)), std::runtime_error);
}

TEST_CASE("TC-BOOK-02: generated PNRs are ten alphanumeric characters and distinct",
          "[unit][M13]") {
    std::unordered_set<std::string> issued;
    for (int i = 0; i < 10000; ++i) {
        const auto pnr = rrs::generate_unique_pnr({}, [&issued](std::string_view candidate) {
            return issued.count(std::string(candidate)) != 0;
        });
        REQUIRE(rrs::is_valid_pnr(pnr));
        REQUIRE(issued.insert(pnr).second);
    }
    CHECK(issued.size() == 10000);
    CHECK_FALSE(rrs::is_valid_pnr("abc1234567"));
    CHECK_FALSE(rrs::is_valid_pnr("A1B2C3D4E"));
}

TEST_CASE("TC-BOOK-02: an injected collision is detected and a fresh PNR issued",
          "[unit][M13]") {
    const std::vector<std::string> candidates{"DUPLICATE1", "DUPLICATE1", "FRESHPNR02"};
    std::size_t next = 0;
    const auto generator = [&] { return candidates.at(next++); };
    const auto pnr = rrs::generate_unique_pnr(
        generator, [](std::string_view candidate) { return candidate == "DUPLICATE1"; });
    CHECK(pnr == "FRESHPNR02");
    CHECK(next == 3);

    CHECK_THROWS_AS(rrs::generate_unique_pnr([] { return std::string("SAMEPNR001"); },
                                             [](std::string_view) { return true; }),
                    std::runtime_error);
}
