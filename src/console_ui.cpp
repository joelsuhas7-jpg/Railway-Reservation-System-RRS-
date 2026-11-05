#include "console_ui.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <stdexcept>
#include <istream>
#include <ostream>
#include <string>
#include <utility>

namespace rrs {

namespace {

const std::vector<MenuItem> kPublicItems = {
    {MenuAction::Register, "Register"},
    {MenuAction::Login, "Login"},
    {MenuAction::Exit, "Exit"},
};

const std::vector<MenuItem> kPassengerItems = {
    {MenuAction::SearchTrains, "Search trains"},
    {MenuAction::ViewTrainDetail, "View train detail and class availability"},
    {MenuAction::BookTicket, "Book ticket"},
    {MenuAction::ViewTicket, "View ticket by PNR"},
    {MenuAction::CancelTicket, "Cancel ticket"},
    {MenuAction::ChangePassword, "Change password"},
    {MenuAction::Logout, "Logout"},
};

const std::vector<MenuItem> kAdministratorItems = {
    {MenuAction::ManageTrains, "Manage trains and schedules"},
    {MenuAction::ManageClasses, "Manage travel classes and capacity"},
    {MenuAction::ManageFares, "Manage class fares"},
    {MenuAction::AdjustSeats, "Adjust seat availability"},
    {MenuAction::BookingReport, "Booking and cancellation report"},
    {MenuAction::ViewAuditLog, "Audit log view"},
    {MenuAction::ResetPassword, "Reset passenger password"},
    {MenuAction::Logout, "Logout"},
};

// Longer input cannot be a valid item number and must not overflow the conversion.
constexpr std::size_t kMaxSelectionDigits = 3;

std::string_view trim(std::string_view text) {
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

const std::vector<MenuItem>& menu_items(MenuTree tree) {
    switch (tree) {
        case MenuTree::Passenger:
            return kPassengerItems;
        case MenuTree::Administrator:
            return kAdministratorItems;
        case MenuTree::Public:
            break;
    }
    return kPublicItems;
}

std::string_view menu_title(MenuTree tree) {
    switch (tree) {
        case MenuTree::Passenger:
            return "Passenger menu";
        case MenuTree::Administrator:
            return "Administrator menu";
        case MenuTree::Public:
            break;
    }
    return "Main menu";
}

const std::vector<std::string_view>& menu_titles() {
    static const std::vector<std::string_view> titles = {
        menu_title(MenuTree::Public), menu_title(MenuTree::Passenger),
        menu_title(MenuTree::Administrator), kManageTrainsMenu, kManageClassesMenu,
        kManageFaresMenu,
    };
    return titles;
}

const std::vector<HelpFrame>& help_frames() {
    static const std::vector<HelpFrame> frames = {
        {"Main menu",
         {"1 Register: create a passenger account with a user id, password and contact.",
          "2 Login: sign in to reach the passenger or administrator menu.",
          "3 Exit: close the application.",
          "Type an option number and press Enter."}},
        {"Passenger menu",
         {"1 Search trains: list trains between two stations on a journey date.",
          "2 View train detail: one train's classes with fare and free seats.",
          "3 Book ticket: book 1 to 6 passengers and receive a PNR.",
          "4 View ticket by PNR: show a booking you made.",
          "5 Cancel ticket: cancel by PNR; the refund depends on time to departure.",
          "6 Change password: replace your password after confirming the current one.",
          "7 Logout: end the session. Sessions also end after 15 idle minutes.",
          "Station codes and PNRs may be typed in lower case. Dates are YYYY-MM-DD."}},
        {"Administrator menu",
         {"1 Manage trains: add, update, remove or list trains and schedules.",
          "2 Manage classes: add classes to a train and set their seat capacity.",
          "3 Manage fares: configure the fare for a train and class.",
          "4 Adjust seats: block seats for a train, class and journey date.",
          "5 Report: bookings, cancellations and totals; settle pending refunds.",
          "6 Audit log view: review security-relevant actions.",
          "7 Reset password: set a new password for a passenger account.",
          "8 Logout: end the session."}},
        {kManageTrainsMenu,
         {"1 Add train: number, name, stations, times (HH:MM) and running days.",
          "2 Update train: change the details of an existing train.",
          "3 Remove train: withdraw a train from search and booking.",
          "4 List active trains: show every train currently offered.",
          "5 Back: return to the administrator menu."}},
        {kManageClassesMenu,
         {"1 Add class: SL, 3A, 2A, 1A, CC or 2S with a positive seat capacity.",
          "2 Update class capacity: change the capacity of an existing class.",
          "3 List classes for a train: show each class with its capacity.",
          "4 Back: return to the administrator menu."}},
        {kManageFaresMenu,
         {"1 Configure or replace fare: amount in rupees and an effective-from date.",
          "2 List fares: show the fare history of a train and class.",
          "3 Back: return to the administrator menu.",
          "A class without a fare is shown to passengers as not open for booking."}},
    };
    return frames;
}

bool is_help_request(std::string_view line) {
    const auto text = trim(line);
    if (text.empty() || text.size() > 4) {
        return false;
    }
    std::string lower(text);
    for (auto& character : lower) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lower == "?" || lower == "h" || lower == "help";
}

void render_help(std::ostream& out, std::string_view menu) {
    const auto& frames = help_frames();
    const auto frame = std::find_if(frames.begin(), frames.end(),
                                    [menu](const HelpFrame& item) { return item.menu == menu; });
    if (frame == frames.end()) {
        throw std::invalid_argument("no help frame for this menu");
    }
    out << "--- Help: " << frame->menu << " ---\n";
    for (const auto& line : frame->lines) {
        out << "  " << line << '\n';
    }
}

bool is_available(MenuTree tree, MenuAction action) {
    const auto& items = menu_items(tree);
    return std::any_of(items.begin(), items.end(),
                       [action](const MenuItem& item) { return item.action == action; });
}

void render_menu(std::ostream& out, MenuTree tree) {
    out << "\n=== " << menu_title(tree) << " ===\n";
    const auto& items = menu_items(tree);
    for (std::size_t i = 0; i < items.size(); ++i) {
        out << "  " << (i + 1) << ". " << items[i].label << '\n';
    }
    out << kHelpHint << '\n';
}

std::optional<MenuAction> parse_selection(std::string_view line, MenuTree tree) {
    const std::string_view text = trim(line);
    if (text.empty() || text.size() > kMaxSelectionDigits) {
        return std::nullopt;
    }
    std::size_t number = 0;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
        number = number * 10 + static_cast<std::size_t>(c - '0');
    }
    const auto& items = menu_items(tree);
    if (number < 1 || number > items.size()) {
        return std::nullopt;
    }
    return items[number - 1].action;
}

Outcome Outcome::success(std::string message, std::string record_id) {
    return {OutcomeKind::Success, std::move(message), std::move(record_id)};
}

Outcome Outcome::validation_failure(std::string field, std::string message) {
    return {OutcomeKind::ValidationFailure, std::move(message), std::move(field)};
}

Outcome Outcome::refusal(std::string message) {
    return {OutcomeKind::BusinessRuleRefusal, std::move(message), {}};
}

Outcome Outcome::system_failure(std::string message) {
    return {OutcomeKind::SystemFailure, std::move(message), {}};
}

void report_outcome(std::ostream& out, std::ostream& err, const Outcome& outcome) {
    switch (outcome.kind) {
        case OutcomeKind::Success:
            out << "OK: " << outcome.message;
            if (!outcome.detail.empty()) {
                out << " (ref: " << outcome.detail << ')';
            }
            out << '\n';
            return;
        case OutcomeKind::ValidationFailure:
            out << "INVALID " << outcome.detail << ": " << outcome.message << '\n';
            return;
        case OutcomeKind::BusinessRuleRefusal:
            out << "REFUSED: " << outcome.message << '\n';
            return;
        case OutcomeKind::SystemFailure:
            out << "SYSTEM ERROR: " << outcome.message << '\n';
            err << "SYSTEM ERROR: " << outcome.message << '\n';
            return;
    }
}

void run_menu(std::istream& in, std::ostream& out, std::ostream& err, MenuTree tree,
              const ActionHandler& handler,
              const std::function<bool(MenuAction)>& should_stop) {
    std::string line;
    while (true) {
        render_menu(out, tree);
        out << "Select an option: " << std::flush;
        if (!std::getline(in, line)) {
            out << '\n';
            return;
        }
        if (is_help_request(line)) {
            render_help(out, menu_title(tree));
            continue;
        }
        const auto action = parse_selection(line, tree);
        if (!action) {
            report_outcome(out, err,
                           Outcome::validation_failure(
                               "menu selection",
                               "enter a number from 1 to " + std::to_string(menu_items(tree).size())));
            continue;
        }
        if (*action == MenuAction::Exit) {
            return;
        }
        Outcome outcome;
        try {
            outcome = handler(*action);
        } catch (const std::exception& error) {
            outcome = Outcome::system_failure(error.what());
        } catch (...) {
            outcome = Outcome::system_failure("unknown operation failure");
        }
        report_outcome(out, err, outcome);
        if (*action == MenuAction::Logout || (should_stop && should_stop(*action))) {
            return;
        }
    }
}

}  // namespace rrs
