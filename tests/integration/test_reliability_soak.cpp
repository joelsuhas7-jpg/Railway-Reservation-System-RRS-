#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "persistence.hpp"
#include "search_flow.hpp"
#include "session_context.hpp"
#include "train_catalogue.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("rrs-reliability-soak-" + std::to_string(nonce));
        std::error_code error;
        if (!std::filesystem::create_directory(path_, error)) {
            throw std::filesystem::filesystem_error("cannot create reliability test directory",
                                                    path_, error);
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

}  // namespace

TEST_CASE("TC-REL-02: system failures stay within 0.1 per cent over 10000 operations",
          "[integration][M6][M7][M8][M9]") {
    constexpr std::uint64_t operation_count = 10000;
    TempDirectory directory;
    const rrs::PersistenceStore store{directory.path()};
    const rrs::AuditLog audit_log{directory.path() / "audit.log"};
    const rrs::UserStore users{store};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));

    const rrs::Session admin{"admin", rrs::Role::Administrator};
    rrs::TrainCatalogueModule trains{store, audit_log};
    REQUIRE(trains
                .add_train(admin, "2026-10-30T10:00:00Z",
                           {12345, "Reliability Express", "SBC", "NDLS", "06:00", "14:00",
                            0x7f})
                .kind == rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes{store, trains, audit_log,
                                       [] { return std::string("2026-10-30T10:00:00Z"); }};
    REQUIRE(classes.add_class(admin, "2026-10-30T10:00:00Z", 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream errors;
    const rrs::AppContext context{input, output, errors, users, audit_log,
                                  [] { return std::string("2026-10-30T10:00:00Z"); }, {}};

    std::uint64_t system_failures = 0;
    for (std::uint64_t operation = 0; operation < operation_count; ++operation) {
        const auto outcome = rrs::show_train_classes(context, 12345, "2026-11-02");
        if (outcome.kind == rrs::OutcomeKind::SystemFailure) {
            ++system_failures;
        }
    }

    INFO("operations=" << operation_count << ", system_failures=" << system_failures);
    CHECK(system_failures * 1000 <= operation_count);
}
