#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "atomic_journal.hpp"

namespace {
class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> next{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-atomic-journal-" + std::to_string(stamp) + "-" +
                     std::to_string(next.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error) throw std::filesystem::filesystem_error(
                "create atomic journal test directory", path_, error);
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
}

TEST_CASE("TC-ROB-03-UNIT-01: journal commits all records across files", "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    store.write_record("bookings.dat", 4, 0, "OLD1");
    store.write_record("inventory.dat", 4, 0, "OLD2");
    rrs::AtomicJournal journal(store);

    journal.commit({{"bookings.dat", 4, 0, "NEW1"},
                    {"inventory.dat", 4, 0, "NEW2"}});

    CHECK(store.read_record("bookings.dat", 4, 0) == "NEW1");
    CHECK(store.read_record("inventory.dat", 4, 0) == "NEW2");
    CHECK_FALSE(std::filesystem::exists(temp.path() / ".rrs-atomic-journal"));
}

TEST_CASE("TC-ROB-03-UNIT-02: recovery replays an interrupted transaction", "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    store.write_record("bookings.dat", 4, 0, "OLD1");
    store.write_record("inventory.dat", 4, 0, "OLD2");
    {
        rrs::AtomicJournal journal(store);
        CHECK_THROWS_AS(journal.commit(
            {{"bookings.dat", 4, 0, "NEW1"}, {"inventory.dat", 4, 0, "NEW2"}},
            [](rrs::JournalStage stage, std::size_t applied) {
                if (stage == rrs::JournalStage::record_applied && applied == 1) {
                    throw std::runtime_error("simulated process interruption");
                }
            }), std::runtime_error);
    }

    rrs::AtomicJournal recovered(store);
    CHECK(store.read_record("bookings.dat", 4, 0) == "NEW1");
    CHECK(store.read_record("inventory.dat", 4, 0) == "NEW2");
    CHECK_FALSE(std::filesystem::exists(temp.path() / ".rrs-atomic-journal"));
}

TEST_CASE("TC-ROB-03-UNIT-03: invalid transactions are rejected before journaling", "[unit][M1]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    store.write_record("bookings.dat", 4, 0, "OLD1");
    rrs::AtomicJournal journal(store);

    CHECK_THROWS_AS(journal.commit({}), std::invalid_argument);
    CHECK_THROWS_AS(journal.commit({{"bookings.dat", 4, 0, "BAD"}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(journal.commit({{"../bookings.dat", 4, 0, "NEW1"}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(journal.commit({{"bookings.dat", 4, 2, "NEW1"}}),
                    std::out_of_range);
    CHECK_THROWS_AS(journal.commit({{"bookings.dat", 4, 0, "NEW1"},
                                    {"bookings.dat", 4, 0, "NEW2"}}),
                    std::invalid_argument);
    CHECK(store.read_record("bookings.dat", 4, 0) == "OLD1");
    CHECK_FALSE(std::filesystem::exists(temp.path() / ".rrs-atomic-journal"));
}
