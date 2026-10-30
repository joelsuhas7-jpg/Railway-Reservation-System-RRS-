#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "authorization.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("rrs-authz-test-" + std::to_string(nonce) + "-" +
                 std::to_string(next_id.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_CASE("TC-SEC-02: passenger sessions are refused and audited at admin operation boundary",
          "[unit][M5]") {
    TempDirectory temp;
    const auto audit_path = temp.path() / "audit.log";
    const rrs::AuditLog audit_log(audit_path);
    const rrs::Session passenger{"rider_1", rrs::Role::Passenger};
    const std::vector<std::string> operations{
        "MANAGE_TRAINS", "MANAGE_CLASSES", "MANAGE_FARES", "ADJUST_SEATS",
        "BOOKING_REPORT", "VIEW_AUDIT_LOG", "RESET_PASSWORD"};

    for (const auto& operation : operations) {
        const auto result = rrs::AuthorizationModule::require_administrator(
            passenger, operation, "2026-10-30T10:00:00", audit_log);
        CHECK_FALSE(result);
        CHECK(result.message == "administrator role required");
    }

    const auto lines = read_lines(audit_path);
    REQUIRE(lines.size() == operations.size());
    for (std::size_t index = 0; index < operations.size(); ++index) {
        CHECK(lines[index] == "2026-10-30T10:00:00\trider_1\tPASSENGER\tACCESS_DENIED_" +
                                  operations[index] + "\t-");
    }
}

TEST_CASE("TC-SEC-02: administrator sessions pass without creating denial audit entries",
          "[unit][M5]") {
    TempDirectory temp;
    const auto audit_path = temp.path() / "audit.log";
    const rrs::AuditLog audit_log(audit_path);
    const rrs::Session administrator{"admin", rrs::Role::Administrator};

    const auto result = rrs::AuthorizationModule::require_administrator(
        administrator, "MANAGE_TRAINS", "2026-10-30T10:00:00", audit_log);

    CHECK(result);
    CHECK(result.message == "authorized");
    CHECK_FALSE(std::filesystem::exists(audit_path));
}

TEST_CASE("TC-SEC-02: invalid operation names are rejected before authorization",
          "[unit][M5]") {
    TempDirectory temp;
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    const rrs::Session passenger{"rider_1", rrs::Role::Passenger};

    CHECK_THROWS_AS(rrs::AuthorizationModule::require_administrator(
                        passenger, "", "2026-10-30T10:00:00", audit_log),
                    std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(temp.path() / "audit.log"));
}
