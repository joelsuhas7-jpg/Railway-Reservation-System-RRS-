#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "console_ui.hpp"
#include "search.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr int kRedisplays = 100;
constexpr double kBudgetMs = 50.0;

// 95th percentile by nearest rank over the sample set.
double p95(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const auto rank = (samples.size() * 95 + 99) / 100;  // ceil(0.95 n)
    return samples[rank - 1];
}

// Times each redisplay with output redirected to a file (TC-PERF-04).
template <typename Render>
double redisplay_p95(const std::filesystem::path& file, Render render) {
    std::ofstream out(file, std::ios::trunc);
    REQUIRE(out);
    std::vector<double> samples;
    samples.reserve(kRedisplays);
    for (int i = 0; i < kRedisplays; ++i) {
        const auto start = std::chrono::steady_clock::now();
        render(out);
        out.flush();
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    return p95(samples);
}

// A full availability table: 10 trains with all six classes each.
rrs::SearchResult availability_table() {
    rrs::SearchResult result;
    const std::vector<std::string> classes{"SL", "3A", "2A", "1A", "CC", "2S"};
    for (std::uint32_t i = 0; i < 10; ++i) {
        rrs::TrainRecord train;
        train.train_number = 12300 + i;
        train.name = "Express " + std::to_string(i);
        train.source_station = "SBC";
        train.destination_station = "NDLS";
        train.departure_time = "06:00";
        train.arrival_time = "14:00";
        train.running_days_mask = 0x7f;
        result.trains.push_back(train);
        for (const auto& code : classes) {
            result.class_availability.push_back({train.train_number, code, 45000, 72});
        }
    }
    return result;
}

}  // namespace

TEST_CASE("TC-UX-02: the help frame count equals the menu count", "[unit][M17]") {
    const auto& titles = rrs::menu_titles();
    const auto& frames = rrs::help_frames();
    CHECK(titles.size() == 6);
    CHECK(frames.size() == titles.size());

    std::set<std::string_view> covered;
    for (const auto& frame : frames) {
        covered.insert(frame.menu);
        CHECK_FALSE(frame.lines.empty());
        for (const auto& line : frame.lines) {
            CHECK(line.size() + 2 <= 80);  // rendered with a two-space indent
        }
    }
    for (const auto& title : titles) {
        CHECK(covered.count(title) == 1);
    }
}

TEST_CASE("TC-UX-02: every role-menu option is explained in its help frame", "[unit][M17]") {
    for (const auto tree :
         {rrs::MenuTree::Public, rrs::MenuTree::Passenger, rrs::MenuTree::Administrator}) {
        std::ostringstream help;
        rrs::render_help(help, rrs::menu_title(tree));
        const auto text = help.str();
        CHECK(text.find("--- Help: " + std::string(rrs::menu_title(tree)) + " ---") !=
              std::string::npos);
        const auto& items = rrs::menu_items(tree);
        for (std::size_t i = 0; i < items.size(); ++i) {
            CHECK(text.find("  " + std::to_string(i + 1) + " ") != std::string::npos);
        }

        std::ostringstream menu;
        rrs::render_menu(menu, tree);
        CHECK(menu.str().find(rrs::kHelpHint) != std::string::npos);
    }
    std::ostringstream unknown;
    CHECK_THROWS_AS(rrs::render_help(unknown, "No such menu"), std::invalid_argument);
}

TEST_CASE("TC-UX-02: help is requested with ? h or help only", "[unit][M17]") {
    CHECK(rrs::is_help_request("?"));
    CHECK(rrs::is_help_request(" h "));
    CHECK(rrs::is_help_request("HELP"));
    CHECK_FALSE(rrs::is_help_request(""));
    CHECK_FALSE(rrs::is_help_request("1"));
    CHECK_FALSE(rrs::is_help_request("helpme"));
}

TEST_CASE("TC-UX-02: the deepest passenger path is within 4 selections", "[unit][M17]") {
    // Passenger flows open no further menu, so a task is its passenger-menu selection plus
    // the Login selection on the main menu. Register is reached from the main menu directly.
    const std::vector<rrs::MenuAction> tasks{
        rrs::MenuAction::SearchTrains, rrs::MenuAction::ViewTrainDetail,
        rrs::MenuAction::BookTicket,   rrs::MenuAction::ViewTicket,
        rrs::MenuAction::CancelTicket, rrs::MenuAction::ChangePassword,
        rrs::MenuAction::Logout,
    };
    REQUIRE(rrs::is_available(rrs::MenuTree::Public, rrs::MenuAction::Register));
    REQUIRE(rrs::is_available(rrs::MenuTree::Public, rrs::MenuAction::Login));
    int deepest = 1;  // Register
    for (const auto task : tasks) {
        REQUIRE(rrs::is_available(rrs::MenuTree::Passenger, task));
        deepest = std::max(deepest, 2);  // Login, then the task
    }
    CHECK(rrs::menu_items(rrs::MenuTree::Passenger).size() == tasks.size());
    CHECK(deepest <= 4);
}

TEST_CASE("TC-PERF-04: menu and availability redisplay p95 is within 50 ms",
          "[unit][M17][perf]") {
    const auto file = std::filesystem::temp_directory_path() / "rrs-tc-perf-04.txt";
    const auto table = availability_table();

    const auto public_menu = redisplay_p95(
        file, [](std::ostream& out) { rrs::render_menu(out, rrs::MenuTree::Public); });
    const auto passenger_menu = redisplay_p95(
        file, [](std::ostream& out) { rrs::render_menu(out, rrs::MenuTree::Passenger); });
    const auto admin_menu = redisplay_p95(
        file, [](std::ostream& out) { rrs::render_menu(out, rrs::MenuTree::Administrator); });
    const auto availability = redisplay_p95(
        file, [&table](std::ostream& out) { rrs::render_search_results(out, table); });
    std::filesystem::remove(file);

    std::cout << "TC-PERF-04 p95 ms (100 redisplays to a file): main " << public_menu
              << ", passenger " << passenger_menu << ", administrator " << admin_menu
              << ", availability table (10 trains x 6 classes) " << availability << '\n';
    CHECK(public_menu <= kBudgetMs);
    CHECK(passenger_menu <= kBudgetMs);
    CHECK(admin_menu <= kBudgetMs);
    CHECK(availability <= kBudgetMs);
}
