#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "console_ui.hpp"
#include "fare_calculation.hpp"
#include "passenger.hpp"
#include "payment.hpp"
#include "persistence.hpp"
#include "seat_inventory.hpp"

namespace rrs {

inline constexpr std::string_view kBookingsFile = "bookings.dat";
inline constexpr std::size_t kBookingRecordSize = 512;
inline constexpr std::size_t kPnrLength = 10;
inline constexpr std::size_t kPaymentReferenceMaxLength = 32;

// SRS 7.3 booking state model.
enum class BookingStatus { Pending, Confirmed, Rejected, Cancelled };

// "PENDING", "CONFIRMED", "REJECTED" or "CANCELLED".
const char* booking_status_name(BookingStatus status) noexcept;

// Legal transitions only: PENDING to CONFIRMED or REJECTED, and CONFIRMED to CANCELLED.
// REJECTED and CANCELLED are final and no state is re-entered.
bool can_transition(BookingStatus from, BookingStatus to) noexcept;

// One bookings.dat record (SRS 3.4).
struct BookingRecord {
    std::string pnr;
    std::string user_id;
    std::uint32_t train_number{0};
    std::string class_code;
    std::string journey_date;
    std::vector<Passenger> passengers;
    std::vector<std::uint32_t> seat_numbers;  // one per passenger, in passenger order
    std::int64_t total_fare_paise{0};
    std::string payment_reference;
    BookingStatus status{BookingStatus::Pending};
};

// Fixed-length kBookingRecordSize encoding. Fields that do not fit throw std::invalid_argument;
// a corrupt record throws std::runtime_error on decode.
std::string encode_booking(const BookingRecord& record);
BookingRecord decode_booking(std::string_view bytes);

// RRS-F-017 PNR: ten characters from 0-9 and A-Z.
using PnrGenerator = std::function<std::string()>;
bool is_valid_pnr(std::string_view pnr) noexcept;
std::string random_pnr();

// Draws candidates until one is well formed and unused. Uniqueness comes from this check, not
// from the generator. Throws std::runtime_error if no unique PNR is found after 64 draws.
std::string generate_unique_pnr(const PnrGenerator& generator,
                                const std::function<bool(std::string_view)>& exists);

struct BookingRequest {
    std::uint32_t train_number{0};
    std::string class_code;
    std::string journey_date;
    std::vector<Passenger> passengers;
    PaymentRecord payment{PaymentOutcome::failure, {}};
};

// M13 BookingModule: coordinates fare, payment and seat allocation for UC-01.
class BookingModule final {
public:
    BookingModule(PersistenceStore store, const FareCalculationModule& fares,
                  SeatInventoryModule& inventory, const AuditLog& audit_log,
                  PnrGenerator pnr_generator = {});

    // UC-01 M5 to M10. A failed payment is audited and nothing is written (A6). Seats are
    // allocated and the CONFIRMED booking written in one commit inside the inventory lock
    // (RRS-F-016). Success carries the PNR as its record identifier.
    Outcome book(const Session& session, std::string_view timestamp,
                 const BookingRequest& request);

    std::vector<BookingRecord> list_bookings() const;
    std::optional<BookingRecord> find_booking(std::string_view pnr) const;

private:
    PersistenceStore store_;
    const FareCalculationModule& fares_;
    SeatInventoryModule& inventory_;
    const AuditLog& audit_log_;
    PnrGenerator pnr_generator_;
};

}  // namespace rrs
