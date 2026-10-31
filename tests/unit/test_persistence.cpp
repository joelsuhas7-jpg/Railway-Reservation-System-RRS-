#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "persistence.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-persistence-test-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error(
                    "cannot create persistence test directory", path_, error);
            }
        }
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

constexpr std::array<std::string_view, 7> kRecordFiles = {
    "users.dat", "trains.dat", "classes.dat", "fares.dat",
    "inventory.dat", "bookings.dat", "cancellations.dat"};

}  // namespace

TEST_CASE("TC-ROB-03-01: all seven record files support indexed append and read",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());

    for (const auto file : kRecordFiles) {
        CAPTURE(file);
        const std::string first = "AAAA";
        const std::string second = "BBBB";
        store.write_record(file, 4, 0, first);
        store.write_record(file, 4, 1, second);

        REQUIRE(store.record_count(file, 4) == 2);
        REQUIRE(store.read_record(file, 4, 0) == first);
        REQUIRE(store.read_record(file, 4, 1) == second);
        REQUIRE_FALSE(store.read_record(file, 4, 2).has_value());
    }
}

TEST_CASE("TC-ROB-03-02: indexed read modify write preserves other records",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    store.write_record("bookings.dat", 4, 0, "AAAA");
    store.write_record("bookings.dat", 4, 1, "BBBB");
    store.write_record("bookings.dat", 4, 1, "CCCC");

    REQUIRE(store.record_count("bookings.dat", 4) == 2);
    REQUIRE(store.read_record("bookings.dat", 4, 0) == "AAAA");
    REQUIRE(store.read_record("bookings.dat", 4, 1) == "CCCC");
}

TEST_CASE("TC-ROB-03-03: invalid record sizes and names are rejected",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());

    CHECK_THROWS_AS(store.read_record("users.dat", 0, 0), std::invalid_argument);
    CHECK_THROWS_AS(store.record_count("users.dat", 0), std::invalid_argument);
    CHECK_THROWS_AS(store.write_record("users.dat", 0, 0, ""), std::invalid_argument);
    CHECK_THROWS_AS(store.read_record("users.dat", std::numeric_limits<std::size_t>::max(), 0),
                    std::invalid_argument);
    CHECK_THROWS_AS(store.write_record("users.dat", 4, 0, "TOO LONG"), std::invalid_argument);
    CHECK_THROWS_AS(store.write_record("users.dat", 4, 0, "abc"), std::invalid_argument);
    CHECK_THROWS_AS(store.read_record("../users.dat", 4, 0), std::invalid_argument);
    CHECK_THROWS_AS(store.read_record("subdir/users.dat", 4, 0), std::invalid_argument);
    CHECK_THROWS_AS(store.read_record("", 4, 0), std::invalid_argument);
#ifdef _WIN32
    CHECK_THROWS_AS(store.read_record("..\\users.dat", 4, 0), std::invalid_argument);
    CHECK_THROWS_AS(store.read_record("C:\\users.dat", 4, 0), std::invalid_argument);
#endif
}

TEST_CASE("TC-ROB-03-04: cannot create sparse indexes or accept partial records",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());

    CHECK_THROWS_AS(store.write_record("fares.dat", 4, 1, "BBBB"), std::out_of_range);
    store.write_record("fares.dat", 4, 0, "AAAA");
    {
        std::ofstream malformed(temp.path() / "broken.dat", std::ios::binary);
        malformed << "ABCDE";
    }
    CHECK_THROWS_AS(store.record_count("broken.dat", 4), std::runtime_error);
    CHECK_THROWS_AS(store.read_record("broken.dat", 4, 0), std::runtime_error);
    CHECK_THROWS_AS(store.write_record("broken.dat", 4, 1, "FFFF"), std::runtime_error);
}

TEST_CASE("TC-ROB-03-05: missing files are empty and accept their first record",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());

    CHECK_FALSE(store.read_record("users.dat", 4, 0).has_value());
    CHECK(store.record_count("users.dat", 4) == 0);

    store.write_record("users.dat", 4, 0, "USER");
    CHECK(store.record_count("users.dat", 4) == 1);
    CHECK(store.read_record("users.dat", 4, 0) == "USER");
}

TEST_CASE("TC-ROB-03-06: binary record bytes are preserved without interpretation",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const std::string binary_record{'\0', static_cast<char>(0xff), 'A'};
    const std::string replacement{'B', '\0', static_cast<char>(0x80)};

    store.write_record("inventory.dat", binary_record.size(), 0, binary_record);
    REQUIRE(store.read_record("inventory.dat", binary_record.size(), 0) == binary_record);

    store.write_record("inventory.dat", replacement.size(), 0, replacement);
    CHECK(store.record_count("inventory.dat", replacement.size()) == 1);
    CHECK(store.read_record("inventory.dat", replacement.size(), 0) == replacement);
}

TEST_CASE("TC-ROB-03-07: bulk record read preserves order and rejects partial files",
          "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());

    CHECK(store.read_records("missing.dat", 4).empty());
    {
        std::ofstream empty(temp.path() / "empty.dat", std::ios::binary);
        REQUIRE(empty.good());
    }
    CHECK(store.read_records("empty.dat", 4).empty());
    CHECK_THROWS_AS(store.read_records("records.dat", 0), std::invalid_argument);
    CHECK_THROWS_AS(store.read_records("../records.dat", 4), std::invalid_argument);

    store.write_record("records.dat", 4, 0, "AAAA");
    store.write_record("records.dat", 4, 1, "BBBB");
    store.write_record("records.dat", 4, 2, "CCCC");
    const std::vector<std::string> expected{"AAAA", "BBBB", "CCCC"};
    CHECK(store.read_records("records.dat", 4) == expected);
    CHECK_THROWS_AS(store.read_records("records.dat", 4, 2), std::length_error);

    const auto count_before_change = store.record_count("records.dat", 4);
    store.write_record("records.dat", 4, 3, "DDDD");
    const auto records_after_change = store.read_records("records.dat", 4, 4);
    CHECK(records_after_change.size() != count_before_change);

    {
        std::ofstream malformed(temp.path() / "partial.dat", std::ios::binary);
        malformed << "ABCDE";
    }
    CHECK_THROWS_AS(store.read_records("partial.dat", 4), std::runtime_error);
}
