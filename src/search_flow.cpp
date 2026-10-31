#include "search_flow.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "class_fare_config.hpp"
#include "console_input.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "search.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"

namespace rrs {
namespace {

std::string to_upper(std::string text) {
    for (auto& character : text) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

// The M6 to M8 modules over one data directory, opened for a single console action.
class Catalogue {
public:
    explicit Catalogue(const AppContext& context)
        : store_{context.users.data_directory()},
          trains_{store_, context.audit_log},
          classes_{store_, trains_, context.audit_log, [&context] { return context.now_timestamp(); }},
          fares_{store_, trains_, classes_, context.audit_log,
                 [&context] { return context.now_timestamp(); }},
          inventory_{store_, classes_, context.audit_log,
                     [&context] { return context.now_timestamp(); }} {}

    const TrainCatalogueModule& trains() const { return trains_; }

    // Every configured class of a train on a journey date. A class without an applicable fare
    // is returned with no fare, so it is shown as not open rather than omitted (RRS-F-012).
    std::vector<ClassAvailabilityInput> classes_for(const TrainRecord& train,
                                                    std::string_view journey_date) {
        std::vector<ClassAvailabilityInput> rows;
        const auto configured = classes_.list_classes(train.train_number);
        for (const auto& record : configured.records) {
            const auto fare =
                fares_.find_applicable_fare(train.train_number, record.class_code, journey_date);
            const auto snapshot =
                inventory_.get_inventory(train.train_number, record.class_code, journey_date);
            ClassAvailabilityInput row;
            row.class_code = record.class_code;
            row.capacity = snapshot.total_seats;
            row.allocated_seats = snapshot.total_seats - snapshot.free_seats;
            if (fare.state == FareLookupState::Found && fare.record) {
                row.fare_paise = fare.record->fare_paise;
            }
            rows.push_back(row);
        }
        return rows;
    }

private:
    PersistenceStore store_;
    TrainCatalogueModule trains_;
    ClassFareConfigModule classes_;
    FareConfigModule fares_;
    SeatInventoryModule inventory_;
};

}  // namespace

Outcome search_trains_from_console(const AppContext& context, const Session&,
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
    SearchQuery query;
    if (!read_input("Source station", query.source_station) ||
        !read_input("Destination station", query.destination_station) ||
        !read_input("Journey date (YYYY-MM-DD)", query.journey_date)) {
        return expired() ? Outcome::refusal(std::string(kSessionExpiredMessage))
                         : Outcome::refusal("search cancelled");
    }
    query.source_station = to_upper(query.source_station);
    query.destination_station = to_upper(query.destination_station);
    const auto now = context.now_timestamp();  // YYYY-MM-DDTHH:MM:SSZ
    query.current_date = now.substr(0, 10);
    query.current_time = now.size() >= 16 ? now.substr(11, 5) : std::string{};

    try {
        Catalogue catalogue{context};
        const SearchModule search{catalogue.trains()};
        const auto result =
            search.search(query, {}, [&catalogue](const TrainRecord& train, std::string_view date) {
                return catalogue.classes_for(train, date);
            });
        render_search_results(context.out, result);
        return Outcome::success(std::to_string(result.trains.size()) +
                                (result.trains.size() == 1 ? " train found" : " trains found"));
    } catch (const std::invalid_argument& error) {
        return Outcome::validation_failure("search", error.what());
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome view_train_detail_from_console(const AppContext& context, const Session&,
                                       const SessionActivityCheck& session_activity) {
    const auto expired = [&session_activity] {
        return session_activity && !session_activity(false);
    };
    bool end_of_input = false;
    std::uint32_t train_number = 0;
    const auto train_input =
        read_train_number(context.in, context.out, train_number, end_of_input, session_activity);
    if (end_of_input) {
        return expired() ? Outcome::refusal(std::string(kSessionExpiredMessage))
                         : Outcome::refusal("train detail cancelled");
    }
    if (train_input.kind != OutcomeKind::Success) {
        return train_input;
    }
    std::string journey_date;
    if (expired() || !read_field(context.in, context.out, "Journey date (YYYY-MM-DD)", journey_date) ||
        (session_activity && !session_activity(true))) {
        return expired() ? Outcome::refusal(std::string(kSessionExpiredMessage))
                         : Outcome::refusal("train detail cancelled");
    }
    if (!is_valid_fare_date(journey_date)) {
        return Outcome::validation_failure("journey date", "enter a valid YYYY-MM-DD date");
    }

    try {
        Catalogue catalogue{context};
        const auto train = catalogue.trains().find_train(train_number);
        if (!train) {
            return Outcome::refusal("no active train with that number exists");
        }
        SearchResult result;
        result.trains.push_back(*train);
        for (const auto& row : catalogue.classes_for(*train, journey_date)) {
            result.class_availability.push_back(
                {train->train_number, row.class_code, row.fare_paise,
                 row.capacity - row.allocated_seats});
        }
        render_search_results(context.out, result);
        return Outcome::success("train detail shown", std::to_string(train_number));
    } catch (const std::invalid_argument& error) {
        return Outcome::refusal(error.what());
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

void register_search_flows(MenuRouter& router) {
    router.add(MenuAction::SearchTrains, search_trains_from_console);
    router.add(MenuAction::ViewTrainDetail, view_train_detail_from_console);
}

}  // namespace rrs
