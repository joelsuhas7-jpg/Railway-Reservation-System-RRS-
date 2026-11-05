#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "console_ui.hpp"

using rrs::MenuAction;
using rrs::MenuTree;
using rrs::Outcome;

namespace {

std::vector<std::string_view> labels_of(MenuTree tree) {
    std::vector<std::string_view> labels;
    for (const auto& item : rrs::menu_items(tree)) {
        labels.push_back(item.label);
    }
    return labels;
}

std::string rendered(MenuTree tree) {
    std::ostringstream out;
    rrs::render_menu(out, tree);
    return out.str();
}

bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

const std::vector<std::string_view> kAdminOnlyLabels = {
    "Manage trains and schedules", "Manage travel classes and capacity", "Manage class fares",
    "Adjust seat availability",    "Booking and cancellation report",    "Audit log view",
    "Reset passenger password",
};

const std::vector<std::string_view> kPassengerOnlyLabels = {
    "Search trains", "View train detail and class availability", "Book ticket",
    "View ticket by PNR", "Cancel ticket", "Change password",
};

}  // namespace

TEST_CASE("ENB-04-01: each menu tree lists exactly the SRS s3.1 items in order", "[unit][M17]") {
    CHECK(labels_of(MenuTree::Public) == std::vector<std::string_view>{"Register", "Login", "Exit"});
    CHECK(labels_of(MenuTree::Passenger) ==
          std::vector<std::string_view>{"Search trains", "View train detail and class availability",
                                        "Book ticket", "View ticket by PNR", "Cancel ticket",
                                        "Change password", "Logout"});
    CHECK(labels_of(MenuTree::Administrator) ==
          std::vector<std::string_view>{"Manage trains and schedules",
                                        "Manage travel classes and capacity", "Manage class fares",
                                        "Adjust seat availability", "Booking and cancellation report",
                                        "Audit log view", "Reset passenger password", "Logout"});
}

TEST_CASE("ENB-04-02: rendered menus show own items and no out-of-role items", "[unit][M17]") {
    SECTION("passenger") {
        const auto text = rendered(MenuTree::Passenger);
        for (const auto label : labels_of(MenuTree::Passenger)) {
            CHECK(contains(text, label));
        }
        for (const auto label : kAdminOnlyLabels) {
            CHECK_FALSE(contains(text, label));
        }
        CHECK_FALSE(contains(text, "Register"));
    }
    SECTION("administrator") {
        const auto text = rendered(MenuTree::Administrator);
        for (const auto label : labels_of(MenuTree::Administrator)) {
            CHECK(contains(text, label));
        }
        for (const auto label : kPassengerOnlyLabels) {
            CHECK_FALSE(contains(text, label));
        }
        CHECK_FALSE(contains(text, "Register"));
    }
    SECTION("public") {
        const auto text = rendered(MenuTree::Public);
        for (const auto label : kAdminOnlyLabels) {
            CHECK_FALSE(contains(text, label));
        }
        for (const auto label : kPassengerOnlyLabels) {
            CHECK_FALSE(contains(text, label));
        }
        CHECK_FALSE(contains(text, "Logout"));
    }
    SECTION("every line fits in 80 columns") {
        for (const auto tree : {MenuTree::Public, MenuTree::Passenger, MenuTree::Administrator}) {
            std::istringstream lines(rendered(tree));
            std::string line;
            while (std::getline(lines, line)) {
                CHECK(line.size() <= 80);
            }
        }
    }
}

TEST_CASE("ENB-04-03: availability is limited to the role's menu tree", "[unit][M17]") {
    CHECK(rrs::is_available(MenuTree::Passenger, MenuAction::BookTicket));
    CHECK(rrs::is_available(MenuTree::Administrator, MenuAction::ResetPassword));
    CHECK(rrs::is_available(MenuTree::Public, MenuAction::Login));

    CHECK_FALSE(rrs::is_available(MenuTree::Passenger, MenuAction::ResetPassword));
    CHECK_FALSE(rrs::is_available(MenuTree::Passenger, MenuAction::ManageFares));
    CHECK_FALSE(rrs::is_available(MenuTree::Administrator, MenuAction::BookTicket));
    CHECK_FALSE(rrs::is_available(MenuTree::Public, MenuAction::SearchTrains));
    CHECK_FALSE(rrs::is_available(MenuTree::Public, MenuAction::Logout));
}

TEST_CASE("ENB-04-04: selection accepts 1..n and rejects everything else", "[unit][M17]") {
    SECTION("valid boundaries and surrounding whitespace") {
        CHECK(rrs::parse_selection("1", MenuTree::Passenger) == MenuAction::SearchTrains);
        CHECK(rrs::parse_selection("7", MenuTree::Passenger) == MenuAction::Logout);
        CHECK(rrs::parse_selection(" 2 ", MenuTree::Public) == MenuAction::Login);
        CHECK(rrs::parse_selection("8", MenuTree::Administrator) == MenuAction::Logout);
    }
    SECTION("invalid input") {
        CHECK_FALSE(rrs::parse_selection("0", MenuTree::Passenger));
        CHECK_FALSE(rrs::parse_selection("8", MenuTree::Passenger));
        CHECK_FALSE(rrs::parse_selection("4", MenuTree::Public));
        CHECK_FALSE(rrs::parse_selection("", MenuTree::Public));
        CHECK_FALSE(rrs::parse_selection("   ", MenuTree::Public));
        CHECK_FALSE(rrs::parse_selection("abc", MenuTree::Public));
        CHECK_FALSE(rrs::parse_selection("-1", MenuTree::Public));
        CHECK_FALSE(rrs::parse_selection("1a", MenuTree::Public));
        CHECK_FALSE(rrs::parse_selection("99999999999999999999", MenuTree::Public));
    }
}

TEST_CASE("ENB-04-05: success and validation outcomes go to stdout only", "[unit][M17]") {
    std::ostringstream out;
    std::ostringstream err;

    rrs::report_outcome(out, err, Outcome::success("booking confirmed", "AB12CD34EF"));
    CHECK(out.str() == "OK: booking confirmed (ref: AB12CD34EF)\n");

    out.str("");
    rrs::report_outcome(out, err, Outcome::success("logged out"));
    CHECK(out.str() == "OK: logged out\n");

    out.str("");
    rrs::report_outcome(out, err, Outcome::validation_failure("age", "must be between 1 and 120"));
    CHECK(out.str() == "INVALID age: must be between 1 and 120\n");

    CHECK(err.str().empty());
}

TEST_CASE("ENB-04-06: system failures are also written to stderr, refusals are not",
          "[unit][M17]") {
    std::ostringstream out;
    std::ostringstream err;

    rrs::report_outcome(out, err, Outcome::refusal("insufficient free seats"));
    CHECK(out.str() == "REFUSED: insufficient free seats\n");
    CHECK(err.str().empty());

    out.str("");
    rrs::report_outcome(out, err, Outcome::system_failure("could not lock inventory.dat"));
    CHECK(out.str() == "SYSTEM ERROR: could not lock inventory.dat\n");
    CHECK(err.str() == "SYSTEM ERROR: could not lock inventory.dat\n");
}

TEST_CASE("ENB-04-07: menu loop dispatches selections and stops on Exit, Logout or end of input",
          "[unit][M17]") {
    std::ostringstream out;
    std::ostringstream err;
    std::vector<MenuAction> handled;
    const rrs::ActionHandler record = [&handled](MenuAction action) {
        handled.push_back(action);
        return Outcome::success("done");
    };

    SECTION("invalid choice reports a validation failure, then Exit ends the loop") {
        std::istringstream in("9\n3\n");
        rrs::run_menu(in, out, err, MenuTree::Public, record);
        CHECK(contains(out.str(), "INVALID menu selection: enter a number from 1 to 3"));
        CHECK(handled.empty());
        CHECK(err.str().empty());
    }
    SECTION("a valid choice reaches the handler and its outcome is reported") {
        std::istringstream in("2\n3\n");
        rrs::run_menu(in, out, err, MenuTree::Public, record);
        CHECK(handled == std::vector<MenuAction>{MenuAction::Login});
        CHECK(contains(out.str(), "OK: done"));
    }
    SECTION("Logout runs its handler and returns") {
        std::istringstream in("7\n1\n");
        rrs::run_menu(in, out, err, MenuTree::Passenger, record);
        CHECK(handled == std::vector<MenuAction>{MenuAction::Logout});
    }
    SECTION("end of input ends the loop without dispatching") {
        std::istringstream in("");
        rrs::run_menu(in, out, err, MenuTree::Administrator, record);
        CHECK(handled.empty());
    }
}

TEST_CASE("TC-ROB-01: handled operation failures return to the menu", "[unit][M17]") {
    std::string selections;
    constexpr int kFaults = 20;
    for (int attempt = 0; attempt < kFaults; ++attempt) {
        selections += "2\n";
    }
    selections += "3\n";
    std::istringstream in(selections);
    std::ostringstream out;
    std::ostringstream err;
    int handled = 0;

    rrs::run_menu(in, out, err, MenuTree::Public, [&handled](MenuAction) -> Outcome {
        ++handled;
        throw std::runtime_error("injected operation failure");
    });

    CHECK(handled == kFaults);
    CHECK(out.str().find("SYSTEM ERROR: injected operation failure") != std::string::npos);
    CHECK(err.str().find("SYSTEM ERROR: injected operation failure") != std::string::npos);
    std::size_t menus = 0;
    std::size_t offset = 0;
    while ((offset = out.str().find("=== Main menu ===", offset)) != std::string::npos) {
        ++menus;
        ++offset;
    }
    CHECK(menus == kFaults + 1);
}
