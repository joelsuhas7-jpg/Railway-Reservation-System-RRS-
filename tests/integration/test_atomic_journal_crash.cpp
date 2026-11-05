#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <chrono>
#include <string>
#include <system_error>

#include "atomic_journal.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
class TempDirectory {
public:
    TempDirectory() {
        path_ = std::filesystem::temp_directory_path() / "rrs-atomic-journal-sigkill";
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (!std::filesystem::create_directory(path_, error) || error) {
            throw std::filesystem::filesystem_error("create SIGKILL test directory", path_, error);
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

TEST_CASE("TC-ROB-01: 100 SIGKILLs recover every multi-file transaction within five seconds",
          "[integration][M1]") {
#if defined(__unix__) || defined(__APPLE__)
    TempDirectory temp;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto directory = temp.path() / std::to_string(attempt);
        std::filesystem::create_directory(directory);
        {
            rrs::PersistenceStore initial(directory);
            initial.write_record("bookings.dat", 4, 0, "OLD1");
            initial.write_record("inventory.dat", 4, 0, "OLD2");
        }

        const pid_t child = ::fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            try {
                rrs::PersistenceStore store(directory);
                rrs::AtomicJournal journal(store);
                journal.commit({{"bookings.dat", 4, 0, "NEW1"},
                                {"inventory.dat", 4, 0, "NEW2"}},
                    [](rrs::JournalStage stage, std::size_t applied) {
                        if (stage == rrs::JournalStage::record_applied && applied == 1) {
                            ::kill(::getpid(), SIGKILL);
                        }
                    });
            } catch (...) {
                ::_exit(2);
            }
            ::_exit(0);
        }

        int status = 0;
        REQUIRE(::waitpid(child, &status, 0) == child);
        REQUIRE(WIFSIGNALED(status));
        REQUIRE(WTERMSIG(status) == SIGKILL);

        const auto recovery_started = std::chrono::steady_clock::now();
        rrs::PersistenceStore recovered_store(directory);
        const auto recovery_elapsed = std::chrono::steady_clock::now() - recovery_started;
        CHECK(recovery_elapsed <= std::chrono::seconds(5));
        CHECK(recovered_store.read_record("bookings.dat", 4, 0) == "NEW1");
        CHECK(recovered_store.read_record("inventory.dat", 4, 0) == "NEW2");
        CHECK_FALSE(std::filesystem::exists(directory / ".rrs-atomic-journal"));
    }
#else
    SUCCEED("SIGKILL integration test is only supported on POSIX platforms");
#endif
}
