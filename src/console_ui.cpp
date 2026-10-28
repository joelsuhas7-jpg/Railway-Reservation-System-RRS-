#include "console_ui.hpp"

#include <algorithm>
#include <cctype>
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
              const ActionHandler& handler) {
    std::string line;
    while (true) {
        render_menu(out, tree);
        out << "Select an option: " << std::flush;
        if (!std::getline(in, line)) {
            out << '\n';
            return;
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
        report_outcome(out, err, handler(*action));
        if (*action == MenuAction::Logout) {
            return;
        }
    }
}

}  // namespace rrs
