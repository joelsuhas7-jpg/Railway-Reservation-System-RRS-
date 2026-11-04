#include "cancellation_refund.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fare_config.hpp"
#include "ticket_retrieval.hpp"

namespace rrs {
namespace {

constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kPnrOffset = 1;
constexpr std::size_t kCancelledAtOffset = 11;
constexpr std::size_t kTimestampLength = 20;
constexpr std::size_t kSlabOffset = 31;
constexpr std::size_t kRefundOffset = 32;
constexpr std::size_t kReferenceOffset = 40;
constexpr std::size_t kReservedOffset = kReferenceOffset + kDisbursementReferenceMaxLength;
static_assert(kReservedOffset <= kCancellationRecordSize,
              "cancellation fields exceed the record size");

constexpr std::int64_t kHour = 3600;

bool all_digits(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](char character) { return character >= '0' && character <= '9'; });
}

int number(std::string_view text) {
    int value = 0;
    for (const char character : text) {
        value = value * 10 + (character - '0');
    }
    return value;
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's days_from_civil).
std::int64_t days_from_civil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int year_of_era = year - era * 400;
    const int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + day_of_era - 719468;
}

// Seconds since the epoch for "YYYY-MM-DD" plus hours, minutes and seconds; nullopt if invalid.
std::optional<std::int64_t> epoch_seconds(std::string_view date, int hours, int minutes,
                                          int seconds) {
    if (!is_valid_fare_date(date) || hours > 23 || minutes > 59 || seconds > 59) {
        return std::nullopt;
    }
    const auto days =
        days_from_civil(number(date.substr(0, 4)), number(date.substr(5, 2)), number(date.substr(8, 2)));
    return days * 86400 + hours * kHour + minutes * 60 + seconds;
}

bool valid_reference(std::string_view reference) {
    return !reference.empty() && reference.size() <= kDisbursementReferenceMaxLength &&
           std::none_of(reference.begin(), reference.end(), [](char character) {
               return static_cast<unsigned char>(character) < 0x20;
           });
}

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

void put_text(std::string& bytes, std::size_t offset, std::size_t width, std::string_view text,
              const char* field) {
    if (text.size() > width || text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string(field) + " does not fit the cancellation record");
    }
    bytes.replace(offset, text.size(), text);
}

std::string get_text(std::string_view bytes, std::size_t offset, std::size_t width) {
    const auto field = bytes.substr(offset, width);
    const auto end = field.find('\0');
    if (end != std::string_view::npos &&
        field.find_first_not_of('\0', end) != std::string_view::npos) {
        throw std::runtime_error("corrupt cancellation record: malformed text field");
    }
    return std::string(field.substr(0, end));
}

std::size_t booking_index(const std::vector<BookingRecord>& bookings, std::string_view pnr) {
    const auto found = std::find_if(bookings.begin(), bookings.end(),
                                    [pnr](const BookingRecord& booking) { return booking.pnr == pnr; });
    return static_cast<std::size_t>(found - bookings.begin());
}

}  // namespace

const char* refund_slab_name(RefundSlab slab) noexcept {
    switch (slab) {
        case RefundSlab::S1:
            return "S1";
        case RefundSlab::S2:
            return "S2";
        case RefundSlab::S3:
            return "S3";
        case RefundSlab::S4:
            return "S4";
    }
    return "S4";
}

int refund_percentage(RefundSlab slab) noexcept {
    switch (slab) {
        case RefundSlab::S1:
            return 90;
        case RefundSlab::S2:
            return 50;
        case RefundSlab::S3:
            return 25;
        case RefundSlab::S4:
            return 0;
    }
    return 0;
}

RefundSlab select_refund_slab(std::int64_t seconds_before_departure) noexcept {
    if (seconds_before_departure >= 48 * kHour) {
        return RefundSlab::S1;
    }
    if (seconds_before_departure >= 12 * kHour) {
        return RefundSlab::S2;
    }
    if (seconds_before_departure >= 4 * kHour) {
        return RefundSlab::S3;
    }
    return RefundSlab::S4;
}

std::int64_t refund_paise(std::int64_t total_fare_paise, RefundSlab slab) noexcept {
    // Dividing first keeps the product in range; the remainder term restores exact truncation.
    const std::int64_t percentage = refund_percentage(slab);
    return (total_fare_paise / 100) * percentage + (total_fare_paise % 100) * percentage / 100;
}

std::optional<std::int64_t> seconds_until_departure(std::string_view now_timestamp,
                                                    std::string_view journey_date,
                                                    std::string_view departure_time) {
    if (now_timestamp.size() != 20 || now_timestamp[10] != 'T' || now_timestamp[13] != ':' ||
        now_timestamp[16] != ':' || now_timestamp[19] != 'Z' ||
        !all_digits(now_timestamp.substr(11, 2)) || !all_digits(now_timestamp.substr(14, 2)) ||
        !all_digits(now_timestamp.substr(17, 2))) {
        return std::nullopt;
    }
    if (departure_time.size() != 5 || departure_time[2] != ':' ||
        !all_digits(departure_time.substr(0, 2)) || !all_digits(departure_time.substr(3, 2))) {
        return std::nullopt;
    }
    const auto now = epoch_seconds(now_timestamp.substr(0, 10), number(now_timestamp.substr(11, 2)),
                                   number(now_timestamp.substr(14, 2)),
                                   number(now_timestamp.substr(17, 2)));
    const auto departure = epoch_seconds(journey_date, number(departure_time.substr(0, 2)),
                                         number(departure_time.substr(3, 2)), 0);
    if (!now || !departure) {
        return std::nullopt;
    }
    return *departure - *now;
}

const char* refund_status_name(RefundStatus status) noexcept {
    return status == RefundStatus::Settled ? "SETTLED" : "PENDING";
}

std::string encode_cancellation(const CancellationRecord& record) {
    if (!is_valid_pnr(record.pnr) || record.refund_paise < 0 || record.slab == RefundSlab::S4) {
        throw std::invalid_argument("cancellation PNR, refund or slab is invalid");
    }
    std::string bytes(kCancellationRecordSize, '\0');
    bytes[kVersionOffset] = static_cast<char>(kVersion);
    put_text(bytes, kPnrOffset, kPnrLength, record.pnr, "PNR");
    put_text(bytes, kCancelledAtOffset, kTimestampLength, record.cancelled_at, "cancellation time");
    bytes[kSlabOffset] = static_cast<char>(record.slab);
    const auto refund = static_cast<std::uint64_t>(record.refund_paise);
    for (unsigned int byte = 0; byte < 8; ++byte) {
        bytes[kRefundOffset + byte] = static_cast<char>((refund >> (byte * 8U)) & 0xffU);
    }
    put_text(bytes, kReferenceOffset, kDisbursementReferenceMaxLength,
             record.disbursement_reference, "disbursement reference");
    return bytes;
}

CancellationRecord decode_cancellation(std::string_view bytes) {
    if (bytes.size() != kCancellationRecordSize ||
        static_cast<std::uint8_t>(bytes[kVersionOffset]) != kVersion) {
        throw std::runtime_error("corrupt cancellation record: unsupported size or version");
    }
    if (std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(kReservedOffset), bytes.end(),
                    [](char value) { return value != '\0'; })) {
        throw std::runtime_error("corrupt cancellation record: reserved bytes are not zero");
    }
    CancellationRecord record;
    record.pnr = get_text(bytes, kPnrOffset, kPnrLength);
    record.cancelled_at = get_text(bytes, kCancelledAtOffset, kTimestampLength);
    const auto slab = static_cast<std::uint8_t>(bytes[kSlabOffset]);
    std::uint64_t refund = 0;
    for (unsigned int byte = 0; byte < 8; ++byte) {
        refund |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[kRefundOffset + byte]))
                  << (byte * 8U);
    }
    record.refund_paise = static_cast<std::int64_t>(refund);
    record.disbursement_reference =
        get_text(bytes, kReferenceOffset, kDisbursementReferenceMaxLength);
    if (!is_valid_pnr(record.pnr) || slab >= static_cast<std::uint8_t>(RefundSlab::S4) ||
        record.refund_paise < 0) {
        throw std::runtime_error("corrupt cancellation record: invalid field values");
    }
    record.slab = static_cast<RefundSlab>(slab);
    return record;
}

std::vector<CancellationRecord> read_cancellations(const PersistenceStore& store) {
    std::vector<CancellationRecord> records;
    for (const auto& bytes : store.read_records(kCancellationsFile, kCancellationRecordSize)) {
        records.push_back(decode_cancellation(bytes));
    }
    return records;
}

CancellationRefundModule::CancellationRefundModule(PersistenceStore store,
                                                   const TrainCatalogueModule& trains,
                                                   SeatInventoryModule& inventory,
                                                   const AuditLog& audit_log)
    : store_(std::move(store)), trains_(trains), inventory_(inventory), audit_log_(audit_log) {
    journal_ = std::make_unique<AtomicJournal>(store_);
}

std::optional<CancellationRecord> CancellationRefundModule::find_cancellation(
    std::string_view pnr) const {
    auto records = read_cancellations(store_);
    const auto found = std::find_if(records.begin(), records.end(),
                                    [pnr](const CancellationRecord& record) { return record.pnr == pnr; });
    if (found == records.end()) {
        return std::nullopt;
    }
    return std::optional<CancellationRecord>{std::move(*found)};
}

CancellationQuote CancellationRefundModule::quote(const Session& session, std::string_view now,
                                                  std::string_view pnr) const {
    CancellationQuote result;
    try {
        // M1, M2: the same "no such booking" and audit entry as View ticket (B1, B2).
        auto lookup = TicketRetrievalModule{store_, audit_log_}.retrieve(session, now, pnr);
        if (lookup.outcome.kind != OutcomeKind::Success || !lookup.booking) {
            result.outcome = lookup.outcome;
            return result;
        }
        result.booking = std::move(lookup.booking);
        const auto& booking = *result.booking;
        if (booking.status == BookingStatus::Cancelled) {  // B3
            result.existing = find_cancellation(booking.pnr);
            result.outcome = Outcome::refusal("booking is already cancelled");
            return result;
        }
        if (booking.status != BookingStatus::Confirmed) {  // B4
            result.outcome = Outcome::refusal("booking was never confirmed; no refund arises");
            return result;
        }
        const auto train = trains_.find_train(booking.train_number);
        if (!train) {
            result.outcome = Outcome::system_failure("the booked train's schedule is unavailable");
            return result;
        }
        result.departure = booking.journey_date + " " + train->departure_time;
        const auto seconds =
            seconds_until_departure(now, booking.journey_date, train->departure_time);
        if (!seconds) {
            result.outcome = Outcome::system_failure("cannot compare the clock with the departure");
            return result;
        }
        result.slab = select_refund_slab(*seconds);
        if (result.slab == RefundSlab::S4) {  // B5
            result.outcome = Outcome::refusal(
                "slab S4: cancellation closes 4 hours before departure at " + result.departure);
            return result;
        }
        result.refund_paise = refund_paise(booking.total_fare_paise, result.slab);
        result.outcome = Outcome::success("cancellation allowed", booking.pnr);
        return result;
    } catch (const std::exception& error) {
        result.outcome = Outcome::system_failure(error.what());
        return result;
    }
}

Outcome CancellationRefundModule::cancel(const Session& session, std::string_view now,
                                         std::string_view pnr) {
    try {
        const auto checked = quote(session, now, pnr);
        if (checked.outcome.kind != OutcomeKind::Success || !checked.booking) {
            return checked.outcome;
        }
        const auto& booking = *checked.booking;
        Outcome refused;
        CancellationRecord cancellation;
        const auto released = inventory_.release_seats(
            booking.train_number, booking.class_code, booking.journey_date,
            static_cast<std::uint32_t>(booking.seat_numbers.size()),
            [&](const SeatInventorySnapshot&) -> std::optional<std::vector<RecordWrite>> {
                // The status read before the lock is stale; only this re-read decides.
                auto bookings = read_bookings(store_);
                const auto index = booking_index(bookings, booking.pnr);
                if (index == bookings.size() ||
                    !can_transition(bookings[index].status, BookingStatus::Cancelled)) {
                    refused = Outcome::refusal("booking is already cancelled");
                    return std::nullopt;
                }
                auto updated = bookings[index];
                updated.status = BookingStatus::Cancelled;
                cancellation = {booking.pnr, std::string(now), checked.slab, checked.refund_paise,
                                {}};
                return std::vector<RecordWrite>{
                    RecordWrite{std::string(kBookingsFile), kBookingRecordSize, index,
                                encode_booking(updated)},
                    RecordWrite{std::string(kCancellationsFile), kCancellationRecordSize,
                                store_.record_count(kCancellationsFile, kCancellationRecordSize),
                                encode_cancellation(cancellation)}};
            });
        if (!released) {
            return refused;
        }
        try {
            audit_log_.append({std::string(now), session.user_id, role_name(session.role),
                               "BOOKING_CANCELLED", booking.pnr});
        } catch (const std::exception& error) {
            return Outcome::system_failure("cancellation persisted but audit logging failed: " +
                                           std::string(error.what()));
        }
        return Outcome::success("booking cancelled", booking.pnr);
    } catch (const std::invalid_argument& error) {
        return Outcome::refusal(error.what());
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome CancellationRefundModule::settle_refund(const Session& session,
                                                std::string_view timestamp, std::string_view pnr,
                                                std::string_view disbursement_reference) {
    try {
        if (!valid_reference(disbursement_reference)) {
            return Outcome::validation_failure("disbursement reference",
                                               "must be 1 to 32 printable characters");
        }
        const auto lookup = TicketRetrievalModule{store_, audit_log_}.retrieve(session, timestamp, pnr);
        if (lookup.outcome.kind != OutcomeKind::Success) {
            return lookup.outcome;
        }
        const auto lock = store_.lock_file(kCancellationsFile);
        auto records = read_cancellations(store_);
        const auto found = std::find_if(records.begin(), records.end(),
                                        [pnr](const CancellationRecord& record) { return record.pnr == pnr; });
        if (found == records.end()) {
            return Outcome::refusal("booking has no cancellation to settle");
        }
        if (found->status() == RefundStatus::Settled) {
            return Outcome::refusal("refund is already settled");
        }
        found->disbursement_reference = std::string(disbursement_reference);
        journal_->commit(std::vector<RecordWrite>{
            RecordWrite{std::string(kCancellationsFile), kCancellationRecordSize,
                        static_cast<std::size_t>(found - records.begin()),
                        encode_cancellation(*found)}});
        try {
            audit_log_.append({std::string(timestamp), session.user_id, role_name(session.role),
                               "REFUND_SETTLED", std::string(pnr)});
        } catch (const std::exception& error) {
            return Outcome::system_failure("settlement persisted but audit logging failed: " +
                                           std::string(error.what()));
        }
        return Outcome::success("refund settled", std::string(pnr));
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

}  // namespace rrs
