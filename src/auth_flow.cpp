#include "auth_flow.hpp"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <system_error>
#include <utility>

#include "audit_log.hpp"
#include "authorization.hpp"
#include "class_fare_config.hpp"
#include "fare_config.hpp"
#include "train_catalogue.hpp"

namespace rrs {
namespace {

constexpr auto kSessionIdleTimeout = std::chrono::minutes(15);

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

bool read_field(std::istream& in, std::ostream& out, const char* label, std::string& value) {
    out << label << ": " << std::flush;
    if (!std::getline(in, value)) {
        out << '\n';
        return false;
    }
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return true;
}

bool parse_menu_number(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parse_train_number(std::string_view text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0;
}

bool parse_running_days(std::string_view text, std::uint8_t& mask) {
    if (text.size() != 7) {
        return false;
    }
    mask = 0;
    for (std::size_t day = 0; day < text.size(); ++day) {
        if (text[day] != '0' && text[day] != '1') {
            return false;
        }
        if (text[day] == '1') {
            mask |= static_cast<std::uint8_t>(1U << day);
        }
    }
    return true;
}

bool read_train_details(std::istream& in, std::ostream& out, TrainRecord& train,
                        Outcome& failure, bool& end_of_input,
                        const SessionActivityCheck& session_activity = {}) {
    std::string running_days;
    const auto read_interactive_field = [&](const char* label, std::string& value) {
        if (session_activity && !session_activity(false)) {
            return false;
        }
        if (!read_field(in, out, label, value)) {
            return false;
        }
        return !session_activity || session_activity(true);
    };
    if (!read_interactive_field("Train name", train.name) ||
        !read_interactive_field("Source station", train.source_station) ||
        !read_interactive_field("Destination station", train.destination_station) ||
        !read_interactive_field("Departure time (HH:MM)", train.departure_time) ||
        !read_interactive_field("Arrival time (HH:MM)", train.arrival_time) ||
        !read_interactive_field("Running days (7 digits, Monday-Sunday; 1=run)", running_days)) {
        if (session_activity && !session_activity(false)) {
            end_of_input = true;
            failure = Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
            return false;
        }
        end_of_input = true;
        failure = Outcome::refusal("train operation cancelled");
        return false;
    }
    if (!parse_running_days(running_days, train.running_days_mask)) {
        failure = Outcome::validation_failure(
            "running days", "enter exactly seven 0/1 digits from Monday through Sunday");
        return false;
    }
    return true;
}

Outcome read_train_number(std::istream& in, std::ostream& out, std::uint32_t& train_number,
                          bool& end_of_input,
                          const SessionActivityCheck& session_activity = {}) {
    std::string value;
    if (session_activity && !session_activity(false)) {
        end_of_input = true;
        return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
    }
    if (!read_field(in, out, "Train number", value)) {
        end_of_input = true;
        return Outcome::refusal("train operation cancelled");
    }
    if (session_activity && !session_activity(true)) {
        end_of_input = true;
        return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
    }
    if (!parse_train_number(value, train_number)) {
        return Outcome::validation_failure("train number", "enter a nonzero 32-bit integer");
    }
    return Outcome::success("train number read");
}

}  // namespace

std::optional<AccountForm> read_account_form(std::istream& in, std::ostream& out) {
    AccountForm form;
    if (!read_field(in, out, "User id", form.user_id) ||
        !read_field(in, out, "Password", form.password) ||
        !read_field(in, out, "Contact", form.contact)) {
        return std::nullopt;
    }
    return form;
}

Outcome to_outcome(const AuthResult& result, const std::string& record_id) {
    switch (result.status) {
        case AuthStatus::Ok:
            return Outcome::success(result.message, record_id);
        case AuthStatus::InvalidInput:
            return Outcome::validation_failure(result.field, result.message);
        case AuthStatus::Refused:
            break;
    }
    return Outcome::refusal(result.message);
}

bool run_bootstrap(std::istream& in, std::ostream& out, std::ostream& err,
                   const UserStore& users, const AuditLog& audit_log,
                   const AuditTimestampProvider& timestamp_provider) {
    if (!bootstrap_required(users)) {
        return true;
    }
    out << "First run: create the administrator account.\n";
    while (true) {
        const auto form = read_account_form(in, out);
        if (!form) {
            return false;
        }
        const auto result = bootstrap_admin(users, form->user_id, form->password, form->contact);
        report_outcome(out, err, to_outcome(result, form->user_id));
        if (result) {
            audit_log.append({timestamp_provider(), form->user_id, "ADMIN",
                              "ADMIN_BOOTSTRAPPED", form->user_id});
            return true;
        }
    }
}

Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                              const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider) {
    const auto form = read_account_form(in, out);
    if (!form) {
        return Outcome::refusal("registration cancelled");
    }
    try {
        const auto result = register_passenger(users, form->user_id, form->password, form->contact);
        if (result) {
            audit_log.append({timestamp_provider(), form->user_id, "PASSENGER",
                              "PASSENGER_REGISTERED", form->user_id});
        }
        return to_outcome(result, form->user_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

ConsoleLogin login_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                                const AuditLog& audit_log,
                                const AuditTimestampProvider& timestamp_provider) {
    std::string user_id;
    std::string password;
    if (!read_field(in, out, "User id", user_id) || !read_field(in, out, "Password", password)) {
        return {Outcome::refusal("login cancelled"), std::nullopt};
    }
    try {
        auto login = authenticate(users, user_id, password);
        const auto account = users.find(user_id);
        const auto actor = login.session
                               ? std::string_view(login.session->user_id)
                               : (validate_user_id(user_id) ? user_id : std::string_view("UNKNOWN"));
        const auto role = account ? (account->role == Role::Administrator ? "ADMIN" : "PASSENGER")
                                  : "UNKNOWN";
        audit_log.append({timestamp_provider(), std::string(actor), role,
                          login.session ? "LOGIN_SUCCESS" : "LOGIN_FAILED",
                          account ? account->user_id : "-"});
        if (!login.session) {
            return {to_outcome(login.result, {}), std::nullopt};
        }
        return {Outcome::success(login.result.message, login.session->user_id),
                std::move(login.session)};
    } catch (const std::exception& error) {
        return {Outcome::system_failure(error.what()), std::nullopt};
    }
}

MenuTree menu_for(const Session& session) {
    return session.role == Role::Administrator ? MenuTree::Administrator : MenuTree::Passenger;
}

std::string format_utc_timestamp(std::int64_t epoch_seconds) {
    // Civil date from days since 1970-01-01 (proleptic Gregorian), so no platform gmtime.
    std::int64_t days = epoch_seconds / 86400;
    std::int64_t seconds_of_day = epoch_seconds % 86400;
    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        --days;
    }
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t day_of_era = days - era * 146097;
    const std::int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t month_index = (5 * day_of_year + 2) / 153;
    const std::int64_t day = day_of_year - (153 * month_index + 2) / 5 + 1;
    const std::int64_t month = month_index < 10 ? month_index + 3 : month_index - 9;
    const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);

    std::ostringstream text;
    text << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-'
         << std::setw(2) << day << 'T' << std::setw(2) << seconds_of_day / 3600 << ':'
         << std::setw(2) << seconds_of_day / 60 % 60 << ':' << std::setw(2)
         << seconds_of_day % 60 << 'Z';
    return text.str();
}

std::string utc_timestamp_now() {
    return format_utc_timestamp(std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
}

Outcome manage_trains_from_console(std::istream& in, std::ostream& out, std::ostream& err,
                                   const UserStore& users, const AuditLog& audit_log,
                                   const Session& session,
                                   const AuditTimestampProvider& timestamp_provider,
                                   const SessionActivityCheck& session_activity) {
    PersistenceStore store{users.data_directory()};
    TrainCatalogueModule catalogue{std::move(store), audit_log};
    const auto timestamp = [&timestamp_provider]() {
        return timestamp_provider ? timestamp_provider() : utc_timestamp_now();
    };

    while (true) {
        if (session_activity && !session_activity(false)) {
            return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
        }
        out << "\n=== Manage trains and schedules ===\n"
               "  1. Add train\n"
               "  2. Update train\n"
               "  3. Remove train\n"
               "  4. List active trains\n"
               "  5. Back\n"
               "Select an option: "
            << std::flush;
        std::string selection;
        if (!std::getline(in, selection)) {
            out << '\n';
            return Outcome::refusal("train management cancelled");
        }
        if (session_activity && !session_activity(true)) {
            return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
        }
        if (!selection.empty() && selection.back() == '\r') {
            selection.pop_back();
        }
        int option = 0;
        if (!parse_menu_number(selection, option) || option < 1 || option > 5) {
            report_outcome(out, err, Outcome::validation_failure(
                                         "menu selection", "enter a number from 1 to 5"));
            continue;
        }
        if (option == 5) {
            return Outcome::success("returned from train management");
        }
        if (option == 4) {
            const auto trains = catalogue.list_active_trains();
            if (trains.empty()) {
                out << "No active trains.\n";
                continue;
            }
            out << "Train number\tName\tSource\tDestination\tDeparture\tArrival\tDays\n";
            for (const auto& train : trains) {
                std::string days;
                days.reserve(7);
                for (unsigned int day = 0; day < 7; ++day) {
                    days += (train.running_days_mask & (1U << day)) != 0 ? '1' : '0';
                }
                out << train.train_number << '\t' << train.name << '\t' << train.source_station
                    << '\t' << train.destination_station << '\t' << train.departure_time << '\t'
                    << train.arrival_time << '\t' << days << '\n';
            }
            continue;
        }

        bool end_of_input = false;
        std::uint32_t train_number = 0;
        auto outcome = read_train_number(in, out, train_number, end_of_input, session_activity);
        if (end_of_input) {
            return session_activity && !session_activity(false)
                       ? Outcome::refusal("session expired after 15 minutes of inactivity; please log in again")
                       : outcome;
        }
        if (outcome.kind != OutcomeKind::Success) {
            report_outcome(out, err, outcome);
            continue;
        }

        if (option == 3) {
            outcome = catalogue.remove_train(session, timestamp(), train_number);
            report_outcome(out, err, outcome);
            continue;
        }

        TrainRecord train;
        if (option == 2) {
            const auto existing = catalogue.find_train(train_number);
            if (!existing) {
                report_outcome(out, err, Outcome::refusal("no active train with that number exists"));
                continue;
            }
            train = *existing;
        } else {
            train.train_number = train_number;
        }
        Outcome input_failure;
        if (!read_train_details(in, out, train, input_failure, end_of_input, session_activity)) {
            if (end_of_input) {
                return session_activity && !session_activity(false)
                           ? Outcome::refusal("session expired after 15 minutes of inactivity; please log in again")
                           : input_failure;
            }
            report_outcome(out, err, input_failure);
            continue;
        }
        if (option == 1) {
            outcome = catalogue.add_train(session, timestamp(), train);
        } else {
            outcome = catalogue.update_train(session, timestamp(), train_number, train);
        }
        report_outcome(out, err, outcome);
    }
}

Outcome manage_classes_from_console(std::istream& in, std::ostream& out, std::ostream& err,
                                    const UserStore& users, const AuditLog& audit_log,
                                    const Session& session,
                                    const AuditTimestampProvider& timestamp_provider,
                                    const SessionActivityCheck& session_activity) {
    const PersistenceStore store{users.data_directory()};
    const TrainCatalogueModule trains{store, audit_log};
    ClassFareConfigModule classes{store, trains, audit_log, timestamp_provider};
    const auto timestamp = [&timestamp_provider] {
        return timestamp_provider ? timestamp_provider() : utc_timestamp_now();
    };
    const auto expired = [&session_activity] {
        return session_activity && !session_activity(false);
    };
    const auto read_input = [&](const char* label, std::string& value) {
        if (expired()) {
            return false;
        }
        if (!read_field(in, out, label, value)) {
            return false;
        }
        return !session_activity || session_activity(true);
    };
    const std::string timeout_message =
        "session expired after 15 minutes of inactivity; please log in again";

    while (true) {
        if (expired()) {
            return Outcome::refusal(timeout_message);
        }
        out << "\n=== Manage travel classes and capacity ===\n"
               "  1. Add class\n"
               "  2. Update class capacity\n"
               "  3. List classes for a train\n"
               "  4. Back\n"
               "Select an option: "
            << std::flush;
        std::string selection;
        if (!std::getline(in, selection)) {
            out << '\n';
            return Outcome::refusal("class management cancelled");
        }
        if (session_activity && !session_activity(true)) {
            return Outcome::refusal(timeout_message);
        }
        if (!selection.empty() && selection.back() == '\r') {
            selection.pop_back();
        }
        int option = 0;
        if (!parse_menu_number(selection, option) || option < 1 || option > 4) {
            report_outcome(out, err, Outcome::validation_failure(
                                         "menu selection", "enter a number from 1 to 4"));
            continue;
        }
        if (option == 4) {
            return Outcome::success("returned from class management");
        }

        bool end_of_input = false;
        std::uint32_t train_number = 0;
        auto outcome = read_train_number(in, out, train_number, end_of_input, session_activity);
        if (end_of_input) {
            return expired() ? Outcome::refusal(timeout_message) : outcome;
        }
        if (outcome.kind != OutcomeKind::Success) {
            report_outcome(out, err, outcome);
            continue;
        }
        if (option == 3) {
            const auto result = classes.list_classes(train_number);
            if (!result.train_exists) {
                report_outcome(out, err, Outcome::refusal("no active train with that number exists"));
                continue;
            }
            if (result.records.empty()) {
                out << "No classes configured for train " << train_number << ".\n";
                continue;
            }
            out << "Train number\tClass\tCapacity\n";
            for (const auto& record : result.records) {
                out << record.train_number << '\t' << record.class_code << '\t'
                    << record.capacity << '\n';
            }
            continue;
        }

        std::string class_code;
        std::string capacity_text;
        if (!read_input("Class code (SL, 3A, 2A, 1A, CC, 2S)", class_code)) {
            return expired() ? Outcome::refusal(timeout_message)
                             : Outcome::refusal("class operation cancelled");
        }
        if (!read_input("Capacity", capacity_text)) {
            return expired() ? Outcome::refusal(timeout_message)
                             : Outcome::refusal("class operation cancelled");
        }
        std::int64_t capacity = 0;
        const auto parsed = std::from_chars(capacity_text.data(),
                                            capacity_text.data() + capacity_text.size(), capacity);
        if (parsed.ec != std::errc{} || parsed.ptr != capacity_text.data() + capacity_text.size()) {
            report_outcome(out, err,
                           Outcome::validation_failure("capacity", "enter a signed integer"));
            continue;
        }
        if (expired()) {
            return Outcome::refusal(timeout_message);
        }
        outcome = option == 1 ? classes.add_class(session, timestamp(), train_number, class_code,
                                                  capacity)
                              : classes.update_class(session, timestamp(), train_number, class_code,
                                                     capacity);
        report_outcome(out, err, outcome);
    }
}

Outcome manage_fares_from_console(std::istream& in, std::ostream& out, std::ostream& err,
                                  const UserStore& users, const AuditLog& audit_log,
                                  const Session& session,
                                  const AuditTimestampProvider& timestamp_provider,
                                  const SessionActivityCheck& session_activity) {
    const PersistenceStore store{users.data_directory()};
    const TrainCatalogueModule trains{store, audit_log};
    const ClassFareConfigModule classes{store, trains, audit_log, timestamp_provider};
    const auto timestamp = [&timestamp_provider] {
        return timestamp_provider ? timestamp_provider() : utc_timestamp_now();
    };
    FareConfigModule fares{store, trains, classes, audit_log, timestamp};
    const auto expired = [&session_activity] {
        return session_activity && !session_activity(false);
    };
    const std::string timeout_message =
        "session expired after 15 minutes of inactivity; please log in again";
    const auto read_input = [&](const char* label, std::string& value) {
        if (expired() || !read_field(in, out, label, value)) {
            return false;
        }
        return !session_activity || session_activity(true);
    };
    const auto report_read_failure = [&]() {
        return expired() ? Outcome::refusal(timeout_message)
                         : Outcome::refusal("fare operation cancelled");
    };
    const auto is_back = [](std::string_view value) { return value == "back"; };
    const auto is_cancel = [](std::string_view value) { return value == "cancel"; };

    while (true) {
        if (expired()) {
            return Outcome::refusal(timeout_message);
        }
        out << "\n=== Manage class fares ===\n"
               "  1. Configure or replace fare\n"
               "  2. List fares for a train and class\n"
               "  3. Back\n"
               "Select an option: "
            << std::flush;
        std::string selection;
        if (!std::getline(in, selection)) {
            out << '\n';
            return Outcome::refusal("fare management cancelled");
        }
        if (session_activity && !session_activity(true)) {
            return Outcome::refusal(timeout_message);
        }
        if (!selection.empty() && selection.back() == '\r') {
            selection.pop_back();
        }
        int option = 0;
        if (!parse_menu_number(selection, option) || option < 1 || option > 3) {
            report_outcome(out, err, Outcome::validation_failure(
                                         "menu selection", "enter a number from 1 to 3"));
            continue;
        }
        if (option == 3) {
            return Outcome::success("returned from fare management");
        }

        bool end_of_input = false;
        std::uint32_t train_number = 0;
        auto input_result = read_train_number(in, out, train_number, end_of_input,
                                              session_activity);
        if (end_of_input) {
            return report_read_failure();
        }
        if (input_result.kind != OutcomeKind::Success) {
            report_outcome(out, err, input_result);
            continue;
        }

        std::string class_code;
        if (!read_input("Class code (SL, 3A, 2A, 1A, CC, 2S; back/cancel)", class_code)) {
            return report_read_failure();
        }
        if (is_back(class_code)) {
            return Outcome::success("returned from fare management");
        }
        if (is_cancel(class_code)) {
            out << "Fare operation cancelled.\n";
            continue;
        }
        const auto active_train = trains.find_train(train_number);
        if (!active_train) {
            report_outcome(out, err, Outcome::refusal("no active train with that number exists"));
            continue;
        }
        const auto configured_class = classes.find_class(train_number, class_code);
        if (!configured_class.record) {
            report_outcome(out, err,
                           Outcome::refusal("no configured class with that code exists for the train"));
            continue;
        }

        if (option == 2) {
            const auto records = fares.list_fares(train_number, class_code);
            if (records.empty()) {
                out << kFareUnavailableMessage << '\n';
                continue;
            }
            out << "Train number\tClass\tFare (paise)\tEffective from\n";
            for (const auto& record : records) {
                out << record.train_number << '\t' << record.class_code << '\t'
                    << record.fare_paise << '\t' << record.effective_from << '\n';
            }
            continue;
        }

        std::optional<std::string> replacement_date;
        std::optional<std::uint64_t> replacement_expected_revision;
        std::optional<std::int64_t> replacement_expected_fare;
        const auto existing_fares = fares.list_fares(train_number, class_code);
        const auto current_timestamp = timestamp();
        const std::string current_date = current_timestamp.size() >= 10
                                             ? current_timestamp.substr(0, 10)
                                             : std::string{};
        std::vector<FareRecord> future_fares;
        for (const auto& record : existing_fares) {
            if (record.effective_from > current_date) {
                future_fares.push_back(record);
            }
        }
        if (!future_fares.empty()) {
            out << "Existing future-effective fares:\n"
                   "#\tFare (paise)\tEffective from\tRevision\n";
            for (std::size_t index = 0; index < future_fares.size(); ++index) {
                out << (index + 1) << '\t' << future_fares[index].fare_paise << '\t'
                    << future_fares[index].effective_from << '\t'
                    << future_fares[index].record_revision << '\n';
            }
            out << "Enter 0 to add another date or a fare number to replace that exact record "
                   "(cancel/back also accepted): "
                << std::flush;
            std::string choice;
            if (!read_input("", choice)) {
                return report_read_failure();
            }
            if (is_back(choice)) {
                return Outcome::success("returned from fare management");
            }
            if (is_cancel(choice)) {
                out << "Fare operation cancelled.\n";
                continue;
            }
            std::size_t selected = 0;
            const auto parsed = std::from_chars(choice.data(), choice.data() + choice.size(), selected);
            if (parsed.ec != std::errc{} || parsed.ptr != choice.data() + choice.size() ||
                selected > future_fares.size()) {
                report_outcome(out, err, Outcome::validation_failure(
                                             "fare selection", "enter 0 or a listed fare number"));
                continue;
            }
            if (selected != 0) {
                replacement_date = future_fares[selected - 1].effective_from;
                replacement_expected_revision = future_fares[selected - 1].record_revision;
                replacement_expected_fare = future_fares[selected - 1].fare_paise;
            }
        }

        std::string amount_text;
        if (!read_input("Fare amount in rupees (for example 450.00; back/cancel)", amount_text)) {
            return report_read_failure();
        }
        if (is_back(amount_text)) {
            return Outcome::success("returned from fare management");
        }
        if (is_cancel(amount_text)) {
            out << "Fare operation cancelled.\n";
            continue;
        }
        const auto fare_paise = parse_fare_paise(amount_text);
        if (!fare_paise) {
            report_outcome(out, err, Outcome::validation_failure(
                                         "fare", "enter a positive amount with at most two decimal places"));
            continue;
        }

        std::string effective_from;
        if (!read_input("Effective-from date (YYYY-MM-DD; back/cancel)", effective_from)) {
            return report_read_failure();
        }
        if (is_back(effective_from)) {
            return Outcome::success("returned from fare management");
        }
        if (is_cancel(effective_from)) {
            out << "Fare operation cancelled.\n";
            continue;
        }
        if (expired()) {
            return Outcome::refusal(timeout_message);
        }
        const auto result = replacement_date
                                ? fares.replace_fare(session, train_number, class_code,
                                                     *replacement_date,
                                                     *replacement_expected_revision,
                                                     *replacement_expected_fare,
                                                     *fare_paise,
                                                     effective_from)
                                : fares.configure_fare(session, train_number, class_code,
                                                       *fare_paise, effective_from);
        report_outcome(out, err, result);
    }
}

Outcome change_password_from_console(std::istream& in, std::ostream& out,
                                     const UserStore& users, const Session& session,
                                     const AuditLog& audit) {
    std::string current;
    std::string replacement;
    if (!read_field(in, out, "Current password", current) ||
        !read_field(in, out, "New password", replacement)) {
        return Outcome::refusal("password change cancelled");
    }
    try {
        const auto result = change_password(users, session, current, replacement);
        if (result) {
            audit.append({utc_timestamp_now(), session.user_id, role_name(session.role),
                          "PASSWORD_CHANGE", session.user_id});
        }
        return to_outcome(result, session.user_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome reset_password_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                                    const Session& session, const AuditLog& audit) {
    std::string passenger_id;
    std::string replacement;
    if (!read_field(in, out, "Passenger user id", passenger_id) ||
        !read_field(in, out, "New password", replacement)) {
        return Outcome::refusal("password reset cancelled");
    }
    try {
        const auto result = reset_password(users, session, passenger_id, replacement);
        if (result) {
            audit.append({utc_timestamp_now(), session.user_id, role_name(session.role),
                          "PASSWORD_RESET", passenger_id});
        }
        return to_outcome(result, passenger_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const ActionHandler& role_handler) {
    const InteractiveSessionActionHandler adapter =
        [&role_handler](MenuAction action, const Session&,
                        const SessionActivityCheck&) { return role_handler(action); };
    return login_and_run_session(in, out, err, users, audit_log, timestamp_provider,
                                 adapter);
}

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const InteractiveSessionActionHandler& role_handler,
                              const SessionClockProvider& clock_provider) {
    auto login = login_from_console(in, out, users, audit_log, timestamp_provider);
    if (!login.session) {
        return login.outcome;
    }
    report_outcome(out, err, login.outcome);
    const Session session = std::move(*login.session);
    const auto now = [&clock_provider] {
        return clock_provider ? clock_provider() : std::chrono::steady_clock::now();
    };
    auto last_activity = now();
    bool timed_out = false;
    const SessionActivityCheck session_activity = [&](bool activity) {
        const auto current = now();
        if (timed_out || current - last_activity >= kSessionIdleTimeout) {
            if (!timed_out) {
                audit_log.append({timestamp_provider(), session.user_id, role_name(session.role),
                                  "SESSION_IDLE_TIMEOUT", "-"});
                timed_out = true;
            }
            return false;
        }
        if (activity) {
            last_activity = current;
        }
        return true;
    };
    run_menu(in, out, err, menu_for(session), [&](MenuAction action) {
        if (!session_activity(true)) {
            return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
        }
        return AuthorizationModule::invoke(&session, action, [&](MenuAction authorized_action) {
            switch (authorized_action) {
                case MenuAction::Logout:
                    return Outcome::success("logged out", session.user_id);
                case MenuAction::ChangePassword:
                    return change_password_from_console(in, out, users, session, audit_log);
                case MenuAction::ResetPassword:
                    return reset_password_from_console(in, out, users, session, audit_log);
                default:
                    return role_handler(authorized_action, session, session_activity);
            }
        });
    }, [&](MenuAction) { return timed_out; });
    // The session ends here (RRS-F-003); the caller's public menu is shown next.
    return Outcome::success("returned to the main menu");
}

}  // namespace rrs
