#pragma once

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rrs {

// M17 ConsoleUIModule: menu trees and outcome messages (SRS s3.1).

enum class MenuTree { Public, Passenger, Administrator };

enum class MenuAction {
    // Public
    Register,
    Login,
    Exit,
    // Passenger
    SearchTrains,
    ViewTrainDetail,
    BookTicket,
    ViewTicket,
    CancelTicket,
    ChangePassword,
    // Passenger and administrator
    Logout,
    // Administrator
    ManageTrains,
    ManageClasses,
    ManageFares,
    AdjustSeats,
    BookingReport,
    ViewAuditLog,
    ResetPassword,
};

struct MenuItem {
    MenuAction action{};
    std::string_view label;
};

// Items of a menu tree, in display order. Items outside the tree's role are never listed.
const std::vector<MenuItem>& menu_items(MenuTree tree);
std::string_view menu_title(MenuTree tree);
bool is_available(MenuTree tree, MenuAction action);

// Writes the numbered menu; every line fits in 80 columns.
void render_menu(std::ostream& out, MenuTree tree);

// Parses one input line as a 1-based selection; nullopt if it is not a valid item number.
std::optional<MenuAction> parse_selection(std::string_view line, MenuTree tree);

// RRS-NF-EOU-002: every menu, the three role menus and the administrator sub-menus, offers a
// context help frame shown when "?" is entered at its prompt.
inline constexpr std::string_view kManageTrainsMenu = "Manage trains and schedules";
inline constexpr std::string_view kManageClassesMenu = "Manage travel classes and capacity";
inline constexpr std::string_view kManageFaresMenu = "Manage class fares";
inline constexpr std::string_view kHelpHint = "Enter ? for help on this menu.";

struct HelpFrame {
    std::string_view menu;                 // the menu's title
    std::vector<std::string_view> lines;   // one per option, then input guidance
};

// Every menu title in the application, role menus first.
const std::vector<std::string_view>& menu_titles();
const std::vector<HelpFrame>& help_frames();

// True for "?", "h" or "help" in any case, ignoring surrounding spaces.
bool is_help_request(std::string_view line);

// Writes the help frame of the named menu; an unknown menu is a std::invalid_argument.
void render_help(std::ostream& out, std::string_view menu);

enum class OutcomeKind { Success, ValidationFailure, BusinessRuleRefusal, SystemFailure };

// Every operation ends in exactly one of the four outcomes.
struct Outcome {
    OutcomeKind kind{};
    std::string message;
    std::string detail;  // record identifier (success) or offending field (validation failure)

    static Outcome success(std::string message, std::string record_id = {});
    static Outcome validation_failure(std::string field, std::string message);
    static Outcome refusal(std::string message);
    static Outcome system_failure(std::string message);
};

// Writes the outcome to out; a system failure is also written to err.
void report_outcome(std::ostream& out, std::ostream& err, const Outcome& outcome);

using ActionHandler = std::function<Outcome(MenuAction)>;

// Renders the menu, reads a selection and reports the handler's outcome, until Exit,
// Logout (after its handler runs) or end of input.
void run_menu(std::istream& in, std::ostream& out, std::ostream& err, MenuTree tree,
              const ActionHandler& handler,
              const std::function<bool(MenuAction)>& should_stop = {});

}  // namespace rrs
