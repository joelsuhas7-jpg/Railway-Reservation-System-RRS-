#include "booking.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rrs {
namespace {

// bookings.dat layout, little-endian integers, text fields padded with NUL bytes.
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kStatusOffset = 1;
constexpr std::size_t kPnrOffset = 2;
constexpr std::size_t kUserOffset = 12;
constexpr std::size_t kTrainOffset = 44;
constexpr std::size_t kClassOffset = 48;
constexpr std::size_t kDateOffset = 50;
constexpr std::size_t kCountOffset = 60;
constexpr std::size_t kPassengersOffset = 61;
constexpr std::size_t kNameLength = 60;
constexpr std::size_t kPassengerSize = kNameLength + 2;  // name, age, gender
constexpr std::size_t kSeatsOffset = kPassengersOffset + kMaxPassengers * kPassengerSize;
constexpr std::size_t kFareOffset = kSeatsOffset + kMaxPassengers * 4;
constexpr std::size_t kReferenceOffset = kFareOffset + 8;
constexpr std::size_t kReservedOffset = kReferenceOffset + kPaymentReferenceMaxLength;
static_assert(kReservedOffset <= kBookingRecordSize, "booking fields exceed the record size");

constexpr std::string_view kPnrAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr int kPnrAttempts = 64;

void put_text(std::string& bytes, std::size_t offset, std::size_t width, std::string_view text,
              const char* field) {
    if (text.size() > width || text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string(field) + " does not fit the booking record");
    }
    bytes.replace(offset, text.size(), text);
}

std::string get_text(std::string_view bytes, std::size_t offset, std::size_t width) {
    const auto field = bytes.substr(offset, width);
    const auto end = field.find('\0');
    if (end != std::string_view::npos &&
        field.find_first_not_of('\0', end) != std::string_view::npos) {
        throw std::runtime_error("corrupt booking record: malformed text field");
    }
    return std::string(field.substr(0, end));
}

void put_u64(std::string& bytes, std::size_t offset, std::uint64_t value, unsigned int width) {
    for (unsigned int byte = 0; byte < width; ++byte) {
        bytes[offset + byte] = static_cast<char>((value >> (byte * 8U)) & 0xffU);
    }
}

std::uint64_t get_u64(std::string_view bytes, std::size_t offset, unsigned int width) {
    std::uint64_t value = 0;
    for (unsigned int byte = 0; byte < width; ++byte) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + byte]))
                 << (byte * 8U);
    }
    return value;
}

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

// Lowest seat numbers not held by a confirmed booking on the same train, class and date.
std::vector<std::uint32_t> choose_seats(const std::vector<BookingRecord>& bookings,
                                        const SeatInventorySnapshot& inventory,
                                        std::size_t count) {
    std::set<std::uint32_t> taken;
    for (const auto& booking : bookings) {
        if (booking.status == BookingStatus::Confirmed &&
            booking.train_number == inventory.train_number &&
            booking.class_code == inventory.class_code &&
            booking.journey_date == inventory.journey_date) {
            taken.insert(booking.seat_numbers.begin(), booking.seat_numbers.end());
        }
    }
    std::vector<std::uint32_t> seats;
    for (std::uint32_t seat = 1; seat <= inventory.total_seats && seats.size() < count; ++seat) {
        if (taken.count(seat) == 0) {
            seats.push_back(seat);
        }
    }
    if (seats.size() < count) {
        throw std::runtime_error("seat numbers are inconsistent with the inventory record");
    }
    return seats;
}

}  // namespace

const char* booking_status_name(BookingStatus status) noexcept {
    switch (status) {
        case BookingStatus::Pending:
            return "PENDING";
        case BookingStatus::Confirmed:
            return "CONFIRMED";
        case BookingStatus::Rejected:
            return "REJECTED";
        case BookingStatus::Cancelled:
            return "CANCELLED";
    }
    return "UNKNOWN";
}

bool can_transition(BookingStatus from, BookingStatus to) noexcept {
    switch (from) {
        case BookingStatus::Pending:
            return to == BookingStatus::Confirmed || to == BookingStatus::Rejected;
        case BookingStatus::Confirmed:
            return to == BookingStatus::Cancelled;
        case BookingStatus::Rejected:
        case BookingStatus::Cancelled:
            return false;
    }
    return false;
}

std::string encode_booking(const BookingRecord& record) {
    if (record.passengers.empty() || record.passengers.size() > kMaxPassengers ||
        record.seat_numbers.size() != record.passengers.size()) {
        throw std::invalid_argument("a booking needs 1 to 6 passengers with one seat each");
    }
    if (record.train_number == 0 || record.total_fare_paise < 0) {
        throw std::invalid_argument("booking train number or fare is invalid");
    }
    std::string bytes(kBookingRecordSize, '\0');
    bytes[kVersionOffset] = static_cast<char>(kVersion);
    bytes[kStatusOffset] = static_cast<char>(record.status);
    put_text(bytes, kPnrOffset, kPnrLength, record.pnr, "PNR");
    put_text(bytes, kUserOffset, kUserIdMaxLength, record.user_id, "user id");
    put_u64(bytes, kTrainOffset, record.train_number, 4);
    put_text(bytes, kClassOffset, 2, record.class_code, "class code");
    put_text(bytes, kDateOffset, 10, record.journey_date, "journey date");
    bytes[kCountOffset] = static_cast<char>(record.passengers.size());
    for (std::size_t i = 0; i < record.passengers.size(); ++i) {
        const auto& passenger = record.passengers[i];
        if (passenger.age < 1 || passenger.age > 120) {
            throw std::invalid_argument("passenger age does not fit the booking record");
        }
        const auto offset = kPassengersOffset + i * kPassengerSize;
        put_text(bytes, offset, kNameLength, passenger.name, "passenger name");
        bytes[offset + kNameLength] = static_cast<char>(passenger.age);
        bytes[offset + kNameLength + 1] = static_cast<char>(passenger.gender);
        put_u64(bytes, kSeatsOffset + i * 4, record.seat_numbers[i], 4);
    }
    put_u64(bytes, kFareOffset, static_cast<std::uint64_t>(record.total_fare_paise), 8);
    put_text(bytes, kReferenceOffset, kPaymentReferenceMaxLength, record.payment_reference,
             "payment reference");
    return bytes;
}

BookingRecord decode_booking(std::string_view bytes) {
    if (bytes.size() != kBookingRecordSize ||
        static_cast<std::uint8_t>(bytes[kVersionOffset]) != kVersion) {
        throw std::runtime_error("corrupt booking record: unsupported size or version");
    }
    if (std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(kReservedOffset), bytes.end(),
                    [](char value) { return value != '\0'; })) {
        throw std::runtime_error("corrupt booking record: reserved bytes are not zero");
    }
    const auto status = static_cast<std::uint8_t>(bytes[kStatusOffset]);
    const auto count = static_cast<std::uint8_t>(bytes[kCountOffset]);
    if (status > static_cast<std::uint8_t>(BookingStatus::Cancelled) || count < kMinPassengers ||
        count > kMaxPassengers) {
        throw std::runtime_error("corrupt booking record: invalid status or passenger count");
    }
    BookingRecord record;
    record.status = static_cast<BookingStatus>(status);
    record.pnr = get_text(bytes, kPnrOffset, kPnrLength);
    record.user_id = get_text(bytes, kUserOffset, kUserIdMaxLength);
    record.train_number = static_cast<std::uint32_t>(get_u64(bytes, kTrainOffset, 4));
    record.class_code = get_text(bytes, kClassOffset, 2);
    record.journey_date = get_text(bytes, kDateOffset, 10);
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = kPassengersOffset + i * kPassengerSize;
        Passenger passenger;
        passenger.name = get_text(bytes, offset, kNameLength);
        passenger.age = static_cast<unsigned char>(bytes[offset + kNameLength]);
        const auto gender = static_cast<std::uint8_t>(bytes[offset + kNameLength + 1]);
        if (gender > static_cast<std::uint8_t>(Gender::Other)) {
            throw std::runtime_error("corrupt booking record: invalid passenger gender");
        }
        passenger.gender = static_cast<Gender>(gender);
        record.passengers.push_back(std::move(passenger));
        record.seat_numbers.push_back(
            static_cast<std::uint32_t>(get_u64(bytes, kSeatsOffset + i * 4, 4)));
    }
    record.total_fare_paise = static_cast<std::int64_t>(get_u64(bytes, kFareOffset, 8));
    record.payment_reference = get_text(bytes, kReferenceOffset, kPaymentReferenceMaxLength);
    if (!is_valid_pnr(record.pnr) || record.user_id.empty() || record.train_number == 0 ||
        record.total_fare_paise < 0) {
        throw std::runtime_error("corrupt booking record: invalid field values");
    }
    return record;
}

bool is_valid_pnr(std::string_view pnr) noexcept {
    return pnr.size() == kPnrLength &&
           std::all_of(pnr.begin(), pnr.end(), [](char character) {
               return kPnrAlphabet.find(character) != std::string_view::npos;
           });
}

std::string random_pnr() {
    static std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, kPnrAlphabet.size() - 1);
    std::string pnr(kPnrLength, '0');
    for (auto& character : pnr) {
        character = kPnrAlphabet[pick(engine)];
    }
    return pnr;
}

std::string generate_unique_pnr(const PnrGenerator& generator,
                                const std::function<bool(std::string_view)>& exists) {
    for (int attempt = 0; attempt < kPnrAttempts; ++attempt) {
        auto candidate = generator ? generator() : random_pnr();
        if (is_valid_pnr(candidate) && !(exists && exists(candidate))) {
            return candidate;
        }
    }
    throw std::runtime_error("could not generate a unique PNR");
}

BookingModule::BookingModule(PersistenceStore store, const FareCalculationModule& fares,
                             SeatInventoryModule& inventory, const AuditLog& audit_log,
                             PnrGenerator pnr_generator)
    : store_(std::move(store)), fares_(fares), inventory_(inventory), audit_log_(audit_log),
      pnr_generator_(std::move(pnr_generator)) {}

std::vector<BookingRecord> BookingModule::list_bookings() const {
    std::vector<BookingRecord> bookings;
    for (const auto& bytes : store_.read_records(kBookingsFile, kBookingRecordSize)) {
        bookings.push_back(decode_booking(bytes));
    }
    return bookings;
}

std::optional<BookingRecord> BookingModule::find_booking(std::string_view pnr) const {
    auto bookings = list_bookings();
    const auto found = std::find_if(bookings.begin(), bookings.end(),
                                    [pnr](const BookingRecord& booking) { return booking.pnr == pnr; });
    if (found == bookings.end()) {
        return std::nullopt;
    }
    return std::optional<BookingRecord>{std::move(*found)};
}

Outcome BookingModule::book(const Session& session, std::string_view timestamp,
                            const BookingRequest& request) {
    try {
        if (session.user_id.empty()) {
            return Outcome::refusal("log in to book a ticket");
        }
        const auto passengers = validate_passengers(request.passengers);
        if (!passengers) {
            return Outcome::validation_failure(passengers.field, passengers.message);
        }
        const auto& reference = request.payment.reference;
        if (reference.empty() || reference.size() > kPaymentReferenceMaxLength ||
            std::any_of(reference.begin(), reference.end(), [](char character) {
                return static_cast<unsigned char>(character) < 0x20;
            })) {
            return Outcome::validation_failure("payment reference",
                                               "must be 1 to 32 printable characters");
        }
        const auto quote = fares_.quote(request.train_number, request.class_code,
                                        request.journey_date, request.passengers.size());
        if (!quote.found()) {
            return Outcome::refusal(quote.message);
        }

        BookingRecord record;
        record.user_id = session.user_id;
        record.train_number = request.train_number;
        record.class_code = request.class_code;
        record.journey_date = request.journey_date;
        record.passengers = request.passengers;
        record.total_fare_paise = quote.total_paise;
        record.payment_reference = reference;

        const auto audit = [&](const char* action, const std::string& record_id) {
            audit_log_.append({std::string(timestamp), session.user_id, role_name(session.role),
                               action, record_id});
        };

        // A6: a failed payment rejects the pending booking before any lock is taken.
        if (request.payment.outcome != PaymentOutcome::success) {
            if (!can_transition(record.status, BookingStatus::Rejected)) {
                return Outcome::system_failure("illegal booking state transition");
            }
            audit("BOOKING_REJECTED", reference);
            return Outcome::refusal("payment failed; booking rejected and no seats allocated");
        }
        if (!can_transition(record.status, BookingStatus::Confirmed)) {
            return Outcome::system_failure("illegal booking state transition");
        }

        const auto seat_count = static_cast<std::uint32_t>(request.passengers.size());
        const auto allocation = inventory_.allocate_seats(
            request.train_number, request.class_code, request.journey_date, seat_count,
            [&](const SeatInventorySnapshot& inventory) {
                const auto existing = list_bookings();
                record.seat_numbers = choose_seats(existing, inventory, seat_count);
                // A8: regenerate on collision while the lock is still held.
                record.pnr = generate_unique_pnr(pnr_generator_, [&existing](std::string_view pnr) {
                    return std::any_of(existing.begin(), existing.end(),
                                       [pnr](const BookingRecord& b) { return b.pnr == pnr; });
                });
                record.status = BookingStatus::Confirmed;
                return std::vector<RecordWrite>{RecordWrite{std::string(kBookingsFile),
                                                            kBookingRecordSize, existing.size(),
                                                            encode_booking(record)}};
            });
        if (!allocation.allocated) {
            return Outcome::refusal("insufficient seats: " + std::to_string(seat_count) +
                                    " requested, " + std::to_string(allocation.free_seats) +
                                    " available");
        }
        try {
            audit("BOOKING_CONFIRMED", record.pnr);
        } catch (const std::exception& error) {
            return Outcome::system_failure("booking persisted but audit logging failed: " +
                                           std::string(error.what()));
        }
        return Outcome::success("booking confirmed", record.pnr);
    } catch (const std::invalid_argument& error) {
        return Outcome::refusal(error.what());
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

}  // namespace rrs
