#include "booking_flow.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "class_fare_config.hpp"
#include "console_input.hpp"
#include "fare_calculation.hpp"
#include "fare_config.hpp"
#include "passenger.hpp"
#include "payment.hpp"
#include "persistence.hpp"
#include "search_flow.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"
#include "validation.hpp"

namespace rrs {
namespace {

std::string to_upper(std::string text) {
    for (auto& character : text) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string rupees(std::int64_t paise) {
    std::ostringstream text;
    text << "Rs " << paise / 100 << '.' << std::setw(2) << std::setfill('0') << paise % 100;
    return text.str();
}

const char* gender_name(Gender gender) {
    switch (gender) {
        case Gender::Male:
            return "Male";
        case Gender::Female:
            return "Female";
        case Gender::Other:
            return "Other";
    }
    return "Other";
}

// The M6 to M13 modules over one data directory, opened for a single booking.
class BookingServices {
public:
    explicit BookingServices(const AppContext& context)
        : store_{context.users.data_directory()},
          trains_{store_, context.audit_log},
          classes_{store_, trains_, context.audit_log, [&context] { return context.now_timestamp(); }},
          fares_{store_, trains_, classes_, context.audit_log,
                 [&context] { return context.now_timestamp(); }},
          inventory_{store_, classes_, context.audit_log,
                     [&context] { return context.now_timestamp(); }},
          calculation_{fares_},
          booking_{store_, calculation_, inventory_, context.audit_log} {}

    const FareCalculationModule& calculation() const { return calculation_; }
    SeatInventoryModule& inventory() { return inventory_; }
    BookingModule& booking() { return booking_; }

private:
    PersistenceStore store_;
    TrainCatalogueModule trains_;
    ClassFareConfigModule classes_;
    FareConfigModule fares_;
    SeatInventoryModule inventory_;
    FareCalculationModule calculation_;
    BookingModule booking_;
};

}  // namespace

void render_ticket(std::ostream& out, const BookingRecord& booking) {
    out << "PNR: " << booking.pnr << '\n'
        << "Train " << booking.train_number << " | Class " << booking.class_code
        << " | Journey date " << booking.journey_date << '\n'
        << "Passenger | Age | Gender | Seat\n";
    for (std::size_t i = 0; i < booking.passengers.size(); ++i) {
        const auto& passenger = booking.passengers[i];
        out << passenger.name << " | " << passenger.age << " | " << gender_name(passenger.gender)
            << " | " << booking.class_code << '-' << booking.seat_numbers[i] << '\n';
    }
    out << "Total fare: " << rupees(booking.total_fare_paise) << '\n'
        << "Payment reference: " << booking.payment_reference << '\n'
        << "Status: " << booking_status_name(booking.status) << '\n';
}

Outcome book_ticket_from_console(const AppContext& context, const Session& session,
                                 const SessionActivityCheck& session_activity) {
    const auto expired = [&session_activity] {
        return session_activity && !session_activity(false);
    };
    const auto read_input = [&](const char* label, std::string& value) {
        if (expired() || !read_field(context.in, context.out, label, value)) {
            return false;
        }
        return !session_activity || session_activity(true);
    };
    const auto read_failure = [&] {
        return expired() ? Outcome::refusal(std::string(kSessionExpiredMessage))
                         : Outcome::refusal("booking cancelled");
    };
    // Re-prompts one field until check accepts it (UC-01 A2, A4). False at end of input.
    const auto read_valid = [&](const char* label, std::string& value,
                                const std::function<ValidationResult(const std::string&)>& check) {
        for (;;) {
            if (!read_input(label, value)) {
                return false;
            }
            const auto result = check(value);
            if (result) {
                return true;
            }
            report_outcome(context.out, context.err,
                           Outcome::validation_failure(result.field, result.message));
        }
    };

    // M1, M2: the train and journey date, then its classes with fare and free seats.
    bool end_of_input = false;
    std::uint32_t train_number = 0;
    const auto train_input =
        read_train_number(context.in, context.out, train_number, end_of_input, session_activity);
    if (end_of_input) {
        return read_failure();
    }
    if (train_input.kind != OutcomeKind::Success) {
        return train_input;
    }
    std::string journey_date;
    if (!read_input("Journey date (YYYY-MM-DD)", journey_date)) {
        return read_failure();
    }
    if (!is_valid_fare_date(journey_date)) {
        return Outcome::validation_failure("journey date", "enter a valid YYYY-MM-DD date");
    }
    const auto shown = show_train_classes(context, train_number, journey_date);
    if (shown.kind != OutcomeKind::Success) {
        return shown;
    }

    try {
        BookingServices services{context};

        // M3: class and passenger count.
        std::string class_code;
        if (!read_input("Class code (SL, 3A, 2A, 1A, CC, 2S)", class_code)) {
            return read_failure();
        }
        class_code = to_upper(class_code);
        std::string count_text;
        if (!read_valid("Number of passengers (1-6)", count_text, [](const std::string& text) {
                return validate_integer("passenger count", text,
                                        static_cast<std::int64_t>(kMinPassengers),
                                        static_cast<std::int64_t>(kMaxPassengers));
            })) {
            return read_failure();
        }
        const auto count = static_cast<std::size_t>(std::stoi(count_text));
        const auto quote = services.calculation().quote(train_number, class_code, journey_date, count);
        if (!quote.found()) {
            return Outcome::refusal(quote.message);  // A1: the class is not open for booking
        }
        const auto free =
            services.inventory().get_inventory(train_number, class_code, journey_date).free_seats;
        if (count > free) {  // A3: early check only; the authoritative one is inside the lock
            return Outcome::refusal("insufficient seats: " + std::to_string(count) +
                                    " requested, " + std::to_string(free) + " available");
        }

        // M4: each passenger in turn; a rejected field is asked again on its own.
        std::vector<Passenger> passengers;
        for (std::size_t i = 1; i <= count; ++i) {
            context.out << "Passenger " << i << " of " << count << '\n';
            Passenger passenger;
            std::string age_text;
            std::string gender_text;
            if (!read_valid("Name", passenger.name, [](const std::string& text) {
                    const auto present = validate_presence("name", text);
                    return present ? validate_length("name", text, 1, 60) : present;
                }) ||
                !read_valid("Age", age_text, [](const std::string& text) {
                    return validate_integer("age", text, 1, 120);
                }) ||
                !read_valid("Gender (M/F/O)", gender_text, [](const std::string& text) {
                    const auto code = to_upper(text);
                    if (code == "M" || code == "F" || code == "O") {
                        return ValidationResult{};
                    }
                    return ValidationResult{false, "gender", "enter M, F or O"};
                })) {
                return read_failure();
            }
            passenger.age = std::stoi(age_text);
            const auto gender = to_upper(gender_text);
            passenger.gender = gender == "M"   ? Gender::Male
                               : gender == "F" ? Gender::Female
                                               : Gender::Other;
            passengers.push_back(passenger);
        }

        // M5: the total for confirmation; declining writes nothing (A5).
        context.out << "Total fare: " << rupees(quote.total_paise) << " (" << count << " x "
                    << rupees(quote.fare_paise) << ")\n";
        std::string answer;
        if (!read_valid("Confirm booking at this fare? (y/n)", answer, [](const std::string& text) {
                const auto code = to_upper(text);
                return code == "Y" || code == "N"
                           ? ValidationResult{}
                           : ValidationResult{false, "confirmation", "enter y or n"};
            })) {
            return read_failure();
        }
        if (to_upper(answer) == "N") {
            return Outcome::refusal("booking discarded; no seats were held");
        }

        // M6: the operator supplies the payment outcome and reference (Assumption A-07).
        std::string outcome_text;
        std::string reference;
        if (!read_valid("Payment outcome (success/failure)", outcome_text,
                        [](const std::string& text) {
                            const auto code = to_upper(text);
                            return code == "SUCCESS" || code == "FAILURE"
                                       ? ValidationResult{}
                                       : ValidationResult{false, "payment outcome",
                                                          "enter success or failure"};
                        }) ||
            !read_valid("Payment reference", reference, [](const std::string& text) {
                const auto present = validate_presence("payment reference", text);
                return present ? validate_length("payment reference", text, 1,
                                                 kPaymentReferenceMaxLength)
                               : present;
            })) {
            return read_failure();
        }
        const auto payment = PaymentModule{}.record_outcome(
            to_upper(outcome_text) == "SUCCESS" ? PaymentOutcome::success : PaymentOutcome::failure,
            reference);

        // M7 to M10.
        BookingRequest request;
        request.train_number = train_number;
        request.class_code = class_code;
        request.journey_date = journey_date;
        request.passengers = passengers;
        request.payment = payment;
        const auto booked = services.booking().book(session, context.now_timestamp(), request);
        if (booked.kind == OutcomeKind::Success) {
            if (const auto ticket = services.booking().find_booking(booked.detail)) {
                render_ticket(context.out, *ticket);
            }
        }
        return booked;
    } catch (const std::invalid_argument& error) {
        return Outcome::refusal(error.what());
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

void register_booking_flows(MenuRouter& router) {
    router.add(MenuAction::BookTicket, book_ticket_from_console);
}

}  // namespace rrs
