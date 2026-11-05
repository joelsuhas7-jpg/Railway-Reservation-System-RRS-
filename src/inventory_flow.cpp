#include "inventory_flow.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <system_error>

#include "class_fare_config.hpp"
#include "console_input.hpp"
#include "persistence.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"

namespace rrs {

Outcome adjust_seats_from_console(const AppContext& context, const Session& session,
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
                         : Outcome::refusal("seat adjustment cancelled");
    };

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
    std::string class_code;
    std::string journey_date;
    std::string count_text;
    if (!read_input("Class code (SL, 3A, 2A, 1A, CC, 2S)", class_code) ||
        !read_input("Journey date (YYYY-MM-DD)", journey_date) ||
        !read_input("Seats to block", count_text)) {
        return read_failure();
    }
    std::int64_t seat_count = 0;
    const auto parsed =
        std::from_chars(count_text.data(), count_text.data() + count_text.size(), seat_count);
    if (count_text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != count_text.data() + count_text.size()) {
        return Outcome::validation_failure("seats to block", "enter a whole number");
    }

    try {
        const PersistenceStore store{context.users.data_directory()};
        const TrainCatalogueModule trains{store, context.audit_log};
        const auto timestamp = [&context] { return context.now_timestamp(); };
        const ClassFareConfigModule classes{store, trains, context.audit_log, timestamp};
        SeatInventoryModule inventory{store, classes, context.audit_log, timestamp};
        return inventory.block_seats(session, context.now_timestamp(), train_number, class_code,
                                     journey_date, seat_count);
    } catch (const std::invalid_argument& error) {
        return Outcome::refusal(error.what());
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

void register_inventory_flows(MenuRouter& router) {
    router.add(MenuAction::AdjustSeats, adjust_seats_from_console);
}

}  // namespace rrs
