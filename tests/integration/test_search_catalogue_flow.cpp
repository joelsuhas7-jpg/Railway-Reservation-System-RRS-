#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include "audit_log.hpp"
#include "persistence.hpp"
#include "search.hpp"
#include "train_catalogue.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> id{0};
        const auto prefix = "rrs-search-flow-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    (prefix + "-" + std::to_string(id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create search flow directory",
                                                        path_, error);
            }
        }
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";

}  // namespace

TEST_CASE("TC-SEARCH-01: searches observe M6 additions and removals immediately",
          "[integration][M1][M6][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    rrs::SearchModule search(catalogue);
    const rrs::SearchQuery query{"SBC", "NDLS", "2026-10-30", "2026-10-30", "00:00"};
    CHECK(search.search(query).status == rrs::SearchStatus::NoTrainServesRoute);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const rrs::TrainRecord record{12345, "Karnataka Express", "SBC", "NDLS", "06:00",
                                  "14:00", 0x7f};
    const auto add_result = catalogue.add_train(admin, kTimestamp, record);
    INFO(add_result.message);
    REQUIRE(add_result.kind == rrs::OutcomeKind::Success);
    const auto after_add = search.search(query);
    REQUIRE(after_add.trains.size() == 1);
    CHECK(after_add.trains[0].train_number == 12345);
    auto updated = record;
    updated.departure_time = "07:00";
    REQUIRE(catalogue.update_train(admin, kTimestamp, 12345, updated).kind ==
            rrs::OutcomeKind::Success);
    const auto after_update = search.search(query);
    REQUIRE(after_update.trains.size() == 1);
    CHECK(after_update.trains[0].departure_time == "07:00");
    REQUIRE(catalogue.remove_train(admin, kTimestamp, 12345).kind == rrs::OutcomeKind::Success);
    CHECK(search.search(query).status == rrs::SearchStatus::NoTrainServesRoute);
}
