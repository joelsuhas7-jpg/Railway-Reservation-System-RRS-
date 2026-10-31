#include "search.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <map>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace rrs {
namespace {

struct Date {
    int year;
    unsigned int month;
    unsigned int day;
};

bool leap_year(int year) {
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

bool valid_station_code(std::string_view code) {
    if (code.empty() || code.size() > 8) {
        return false;
    }
    for (const unsigned char character : code) {
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9'))) {
            return false;
        }
    }
    return true;
}

std::optional<Date> parse_date(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9')) {
            return std::nullopt;
        }
    }
    const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                     (text[2] - '0') * 10 + (text[3] - '0');
    const auto two = [&text](std::size_t offset) {
        return static_cast<unsigned int>((text[offset] - '0') * 10 + text[offset + 1] - '0');
    };
    const auto month = two(5);
    const auto day = two(8);
    constexpr std::array<unsigned int, 12> days = {31, 28, 31, 30, 31, 30,
                                                    31, 31, 30, 31, 30, 31};
    if (year == 0 || month < 1 || month > 12) {
        return std::nullopt;
    }
    const unsigned int max_day = days[month - 1] +
                                 ((month == 2 && leap_year(year)) ? 1U : 0U);
    if (day < 1 || day > max_day) {
        return std::nullopt;
    }
    return Date{year, month, day};
}

// Proleptic Gregorian day count; 1970-01-01 was Thursday (Monday=0).
std::int64_t days_from_civil(Date date) {
    int year = date.year;
    year -= date.month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned int year_of_era = static_cast<unsigned int>(year - era * 400);
    const unsigned int adjusted_month = date.month > 2 ? date.month - 3 : date.month + 9;
    const unsigned int day_of_year = (153 * adjusted_month + 2) / 5 + date.day - 1;
    const unsigned int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 +
                                    day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + day_of_era - 719468;
}

std::optional<unsigned int> parse_time(std::string_view text) {
    if (text.size() != 5 || text[2] != ':' || text[0] < '0' || text[0] > '9' ||
        text[1] < '0' || text[1] > '9' || text[3] < '0' || text[3] > '9' ||
        text[4] < '0' || text[4] > '9') {
        return std::nullopt;
    }
    const unsigned int hour = static_cast<unsigned int>((text[0] - '0') * 10 + text[1] - '0');
    const unsigned int minute = static_cast<unsigned int>((text[3] - '0') * 10 + text[4] - '0');
    if (hour > 23 || minute > 59) {
        return std::nullopt;
    }
    return hour * 60 + minute;
}

}  // namespace

SearchResult SearchModule::search(const SearchQuery& query,
                                 const AvailabilityEvaluator& availability) const {
    if (!parse_date(query.journey_date)) {
        throw std::invalid_argument("journey date must be a valid YYYY-MM-DD date");
    }
    const auto current_date = parse_date(query.current_date);
    if (!current_date) {
        throw std::invalid_argument("current date must be a valid YYYY-MM-DD date");
    }
    const auto current_time = parse_time(query.current_time);
    if (!current_time) {
        throw std::invalid_argument("current time must use valid HH:MM format");
    }
    if (!valid_station_code(query.source_station) ||
        !valid_station_code(query.destination_station)) {
        throw std::invalid_argument("station codes must be 1-8 uppercase letters or digits");
    }
    if (query.source_station == query.destination_station) {
        throw std::invalid_argument("source and destination stations must differ");
    }

    const auto trains = catalogue_.list_active_trains();
    using Route = std::pair<std::string, std::string>;
    std::multimap<Route, std::size_t> route_index;
    for (std::size_t i = 0; i < trains.size(); ++i) {
        route_index.emplace(Route{trains[i].source_station, trains[i].destination_station}, i);
    }
    const Route requested{query.source_station, query.destination_station};
    const auto range = route_index.equal_range(requested);
    if (range.first == range.second) {
        return {{}, SearchStatus::NoTrainServesRoute};
    }

    const auto journey_date = *parse_date(query.journey_date);
    const auto weekday_value = static_cast<int>((days_from_civil(journey_date) + 3) % 7);
    const auto weekday = static_cast<unsigned int>((weekday_value + 7) % 7);
    std::vector<TrainRecord> running;
    for (auto it = range.first; it != range.second; ++it) {
        const auto& train = trains[it->second];
        if (std::bitset<7>(train.running_days_mask).test(weekday)) {
            running.push_back(train);
        }
    }
    if (running.empty()) {
        return {{}, SearchStatus::NoTrainRunsOnDate};
    }

    const auto journey_days = days_from_civil(journey_date);
    const auto current_days = days_from_civil(*current_date);
    if (journey_days < current_days) {
        return {{}, SearchStatus::AllDeparturesHavePassed};
    }
    const bool today = journey_days == current_days;
    std::vector<TrainRecord> matches;
    for (const auto& train : running) {
        const auto departure = parse_time(train.departure_time);
        if (!departure) {
            throw std::runtime_error("catalogue contains an invalid departure time");
        }
        if (today && *departure < *current_time) {
            continue;
        }
        matches.push_back(train);
    }
    if (matches.empty()) {
        return {{}, SearchStatus::AllDeparturesHavePassed};
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        if (left.departure_time != right.departure_time) {
            return left.departure_time < right.departure_time;
        }
        return left.train_number < right.train_number;
    });
    if (availability) {
        bool any_unknown = false;
        bool any_available = false;
        for (const auto& train : matches) {
            const auto status = availability(train, query.journey_date);
            if (status == AvailabilityStatus::HasBookableClass) {
                any_available = true;
            }
            any_unknown = any_unknown || status == AvailabilityStatus::Unknown;
        }
        if (!any_available && !any_unknown) {
            return {std::move(matches), SearchStatus::NoClassHasFreeSeats};
        }
    }
    return {std::move(matches), SearchStatus::ResultsFound};
}

void SearchModule::render_results(std::ostream& out, const std::vector<TrainRecord>& trains) {
    out << "Train number\tName\tSource\tDestination\tDeparture\tArrival\n";
    for (const auto& train : trains) {
        out << train.train_number << '\t' << train.name << '\t' << train.source_station << '\t'
            << train.destination_station << '\t' << train.departure_time << '\t'
            << train.arrival_time << '\n';
    }
}

}  // namespace rrs
