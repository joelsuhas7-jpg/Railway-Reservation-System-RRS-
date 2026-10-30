#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "audit_log.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> next_id{0};
        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count();

        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-audit-test-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));

            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }

            if (error) {
                throw std::filesystem::filesystem_error(
                    "cannot create audit test directory", path_, error);
            }
        }
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;

    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    return lines;
}

rrs::AuditEntry valid_entry() {
    return {
        "2026-10-30T10:00:00",
        "joel",
        "ADMIN",
        "BOOKING_CREATED",
        "PNR1234567"
    };
}

}  // namespace

TEST_CASE("TC-AUDIT-01: one action appends exactly one five-field entry",
          "[unit][M3]") {
    TempDirectory temp;
    const auto path = temp.path() / "audit.log";
    rrs::AuditLog log(path);

    log.append(valid_entry());

    const auto lines = read_lines(path);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] ==
          "2026-10-30T10:00:00\tjoel\tADMIN\tBOOKING_CREATED\tPNR1234567");
}

TEST_CASE("TC-AUDIT-02: multiple actions append entries without changing earlier entries",
          "[unit][M3]") {
    TempDirectory temp;
    const auto path = temp.path() / "audit.log";
    rrs::AuditLog log(path);

    auto first = valid_entry();
    auto second = valid_entry();
    second.timestamp = "2026-10-30T10:05:00";
    second.action = "BOOKING_CANCELLED";
    second.record_id = "PNR7654321";

    log.append(first);
    const auto first_snapshot = read_lines(path);

    log.append(second);
    const auto lines = read_lines(path);

    REQUIRE(first_snapshot.size() == 1);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == first_snapshot[0]);
    CHECK(lines[1] ==
          "2026-10-30T10:05:00\tjoel\tADMIN\tBOOKING_CANCELLED\tPNR7654321");
}

TEST_CASE("TC-AUDIT-03: all five audit fields are mandatory",
          "[unit][M3]") {
    TempDirectory temp;
    const auto path = temp.path() / "audit.log";
    rrs::AuditLog log(path);

    const auto valid = valid_entry();

    auto invalid = valid;
    invalid.timestamp.clear();
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    invalid = valid;
    invalid.actor.clear();
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    invalid = valid;
    invalid.role.clear();
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    invalid = valid;
    invalid.action.clear();
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    invalid = valid;
    invalid.record_id.clear();
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("TC-AUDIT-04: fields containing tabs or line breaks are rejected",
          "[unit][M3]") {
    TempDirectory temp;
    const auto path = temp.path() / "audit.log";
    rrs::AuditLog log(path);

    auto invalid = valid_entry();
    invalid.actor = "joel\nanother-user";
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    invalid = valid_entry();
    invalid.action = "BOOKING\tCANCELLED";
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    invalid = valid_entry();
    invalid.record_id = "PNR\r123";
    CHECK_THROWS_AS(log.append(invalid), std::invalid_argument);

    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("TC-AUDIT-05: an existing audit log is preserved when a new entry is appended",
          "[unit][M3]") {
    TempDirectory temp;
    const auto path = temp.path() / "audit.log";

    {
        std::ofstream existing(path, std::ios::binary);
        existing << "old-timestamp\told-actor\tPASSENGER\tLOGIN\tUSER001\n";
    }

    rrs::AuditLog log(path);
    log.append(valid_entry());

    const auto lines = read_lines(path);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] ==
          "old-timestamp\told-actor\tPASSENGER\tLOGIN\tUSER001");
    CHECK(lines[1] ==
          "2026-10-30T10:00:00\tjoel\tADMIN\tBOOKING_CREATED\tPNR1234567");
}

TEST_CASE("TC-AUDIT-06: an invalid audit log path reports a system error",
          "[unit][M3]") {
    TempDirectory temp;
    const auto missing_directory =
        temp.path() / "missing" / "audit.log";

    rrs::AuditLog log(missing_directory);

    CHECK_THROWS_AS(log.append(valid_entry()), std::runtime_error);
}
