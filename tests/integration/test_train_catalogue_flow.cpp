#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "auth_flow.hpp"
#include "audit_log.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-train-flow-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create train flow directory",
                                                        path_, error);
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

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }
    return lines;
}

rrs::TrainRecord train_record() {
    return {12345, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f};
}

}  // namespace

TEST_CASE("TC-ADMIN-01: authorized train lifecycle is durable and audited once per change",
          "[integration][M1][M3][M5][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    rrs::TrainCatalogueModule catalogue(store, audit_log);

    REQUIRE(catalogue.add_train(admin, kTimestamp, train_record()).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(catalogue.list_active_trains().size() == 1);
    auto changed = train_record();
    changed.name = "Updated Express";
    REQUIRE(catalogue.update_train(admin, kTimestamp, 12345, changed).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(catalogue.find_train(12345)->name == "Updated Express");
    REQUIRE(catalogue.remove_train(admin, kTimestamp, 12345).kind ==
            rrs::OutcomeKind::Success);
    CHECK(catalogue.list_active_trains().empty());
    CHECK_FALSE(catalogue.find_train(12345).has_value());

    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_ADDED\t12345");
    CHECK(lines[1] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_UPDATED\t12345");
    CHECK(lines[2] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_REMOVED\t12345");
}

TEST_CASE("TC-ADMIN-01: passenger train mutations are denied and audited by M5",
          "[integration][M1][M3][M5][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const rrs::Session passenger{"rider_1", rrs::Role::Passenger};
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    REQUIRE(catalogue.add_train(admin, kTimestamp, train_record()).kind ==
            rrs::OutcomeKind::Success);
    const auto before = store.read_record("trains.dat", rrs::kTrainRecordSize, 0);
    REQUIRE(before.has_value());

    CHECK(catalogue.add_train(passenger, kTimestamp, train_record()).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    auto updated = train_record();
    updated.name = "Unauthorized Update";
    CHECK(catalogue.update_train(passenger, kTimestamp, 12345, updated).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(catalogue.remove_train(passenger, kTimestamp, 12345).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);

    CHECK(store.read_record("trains.dat", rrs::kTrainRecordSize, 0) == before);
    CHECK(catalogue.find_train(12345)->name == "Karnataka Express");
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_ADDED\t12345");
    CHECK(lines[1] == "2026-10-30T10:00:00Z\trider_1\tPASSENGER\tACCESS_DENIED_ADD_TRAIN\t-");
    CHECK(lines[2] ==
          "2026-10-30T10:00:00Z\trider_1\tPASSENGER\tACCESS_DENIED_UPDATE_TRAIN\t-");
    CHECK(lines[3] ==
          "2026-10-30T10:00:00Z\trider_1\tPASSENGER\tACCESS_DENIED_REMOVE_TRAIN\t-");
}

TEST_CASE("TC-ADMIN-01: removal preserves opaque booking and dependent master records",
          "[integration][M1][M5][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const std::vector<std::string> files = {"bookings.dat", "classes.dat", "fares.dat",
                                             "inventory.dat"};
    const std::string opaque_record = "opaque-record-01";
    for (const auto& file : files) {
        store.write_record(file, opaque_record.size(), 0, opaque_record);
    }
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(catalogue.add_train(admin, kTimestamp, train_record()).kind ==
            rrs::OutcomeKind::Success);

    CHECK(catalogue.remove_train(admin, kTimestamp, 12345).kind == rrs::OutcomeKind::Success);
    for (const auto& file : files) {
        CHECK(store.record_count(file, opaque_record.size()) == 1);
        CHECK(store.read_record(file, opaque_record.size(), 0) == opaque_record);
    }
}

TEST_CASE("TC-ADMIN-01: authenticated administrator adds a train from ManageTrains",
          "[integration][M1][M3][M4][M5][M6][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    const rrs::AuditTimestampProvider fixed_timestamp = [] { return std::string(kTimestamp); };
    std::istringstream in("2\nadmin\nAdminPassword1\n"
                          "1\n1\n12345\nKarnataka Express\nSBC\nNDLS\n"
                          "06:00\n14:00\n1111111\n4\n5\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    std::optional<rrs::Session> received_session;
    const rrs::InteractiveSessionActionHandler administrator_actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            received_session = session;
            if (action == rrs::MenuAction::ManageTrains) {
                return rrs::manage_trains_from_console(in, out, err, users, audit_log, session,
                                                       fixed_timestamp, session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); },
                                              administrator_actions);
        }
        if (action == rrs::MenuAction::Register) {
            return rrs::register_from_console(in, out, users, audit_log,
                                              [] { return std::string(kTimestamp); });
        }
        return rrs::Outcome::success("exit");
    });

    REQUIRE(received_session.has_value());
    CHECK(received_session->user_id == "admin");
    CHECK(received_session->role == rrs::Role::Administrator);
    const rrs::PersistenceStore reopened_store{temp.path()};
    const rrs::TrainCatalogueModule reopened(reopened_store, audit_log);
    const auto train = reopened.find_train(12345);
    REQUIRE(train.has_value());
    CHECK(train->name == "Karnataka Express");
    CHECK(train->running_days_mask == 0x7f);
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tLOGIN_SUCCESS\tadmin");
    CHECK(lines[1] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_ADDED\t12345");
    CHECK(out.str().find("Karnataka Express") != std::string::npos);
    CHECK(err.str().empty());
}

TEST_CASE("TC-ADMIN-01: ManageTrains handles end of input cleanly",
          "[integration][M1][M6][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    std::istringstream in("2\nadmin\nAdminPassword1\n1\n");
    std::ostringstream out;
    std::ostringstream err;
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    const rrs::InteractiveSessionActionHandler administrator_actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            if (action == rrs::MenuAction::ManageTrains) {
                return rrs::manage_trains_from_console(in, out, err, users, audit_log, session,
                                                       [] { return std::string(kTimestamp); },
                                                       session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); },
                                              administrator_actions);
        }
        return rrs::Outcome::success("exit");
    });
    CHECK(err.str().empty());
    CHECK_FALSE(std::filesystem::exists(temp.path() / "trains.dat"));
}

TEST_CASE("TC-ADMIN-01: session expires during train details and requires a new login",
          "[integration][M4][M6][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    std::istringstream in("2\nadmin\nAdminPassword1\n1\n1\n12345\nExpired Express\nSBC\nNDLS\n"
                          "06:00\n14:00\n1111111\n2\nadmin\nAdminPassword1\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    std::size_t clock_calls = 0;
    const rrs::SessionClockProvider clock = [&clock_calls] {
        ++clock_calls;
        using namespace std::chrono;
        const auto elapsed = clock_calls >= 18 ? minutes(16) : minutes(0);
        return steady_clock::time_point{} + elapsed;
    };
    const rrs::InteractiveSessionActionHandler actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            if (action == rrs::MenuAction::ManageTrains) {
                return rrs::manage_trains_from_console(
                    in, out, err, users, audit_log, session,
                    [] { return std::string(kTimestamp); }, session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions,
                                              clock);
        }
        return rrs::Outcome::success("exit");
    });

    CHECK_FALSE(std::filesystem::exists(temp.path() / "trains.dat"));
    CHECK(out.str().find("session expired after 15 minutes of inactivity; please log in again") !=
          std::string::npos);
    const auto output = out.str();
    const auto first_login = output.find("OK: logged in (ref: admin)");
    REQUIRE(first_login != std::string::npos);
    CHECK(output.find("OK: logged in (ref: admin)", first_login + 1) != std::string::npos);
    CHECK(output.find("OK: logged out (ref: admin)") != std::string::npos);
    CHECK(err.str().empty());
}

TEST_CASE("TC-ADMIN-01: active session completes multiple train operations",
          "[integration][M4][M6][M17]") {
    TempDirectory temp;
    const rrs::UserStore users{rrs::PersistenceStore{temp.path()}};
    REQUIRE(rrs::bootstrap_admin(users, "admin", "AdminPassword1", "admin@example.test"));
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    std::istringstream in("2\nadmin\nAdminPassword1\n1\ninvalid\n1\nnot-a-number\n1\n12345\n"
                          "First Express\nSBC\nNDLS\n06:00\n14:00\n1111111\n"
                          "1\n12346\nSecond Express\nSBC\nNDLS\n"
                          "07:00\n15:00\n1111111\n5\n8\n3\n");
    std::ostringstream out;
    std::ostringstream err;
    std::size_t clock_calls = 0;
    const rrs::SessionClockProvider clock = [&clock_calls] {
        ++clock_calls;
        return std::chrono::steady_clock::time_point{} +
               std::chrono::minutes(static_cast<std::int64_t>(clock_calls));
    };
    const rrs::InteractiveSessionActionHandler actions =
        [&](rrs::MenuAction action, const rrs::Session& session,
            const rrs::SessionActivityCheck& session_activity) {
            if (action == rrs::MenuAction::ManageTrains) {
                return rrs::manage_trains_from_console(
                    in, out, err, users, audit_log, session,
                    [] { return std::string(kTimestamp); }, session_activity);
            }
            return rrs::Outcome::refusal("this feature is not available yet");
        };
    rrs::run_menu(in, out, err, rrs::MenuTree::Public, [&](rrs::MenuAction action) {
        if (action == rrs::MenuAction::Login) {
            return rrs::login_and_run_session(in, out, err, users, audit_log,
                                              [] { return std::string(kTimestamp); }, actions,
                                              clock);
        }
        return rrs::Outcome::success("exit");
    });

    const rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit_log};
    REQUIRE(catalogue.list_active_trains().size() == 2);
    CHECK(catalogue.find_train(12345).has_value());
    CHECK(catalogue.find_train(12346).has_value());
    const auto output = out.str();
    CHECK(output.find("session expired") == std::string::npos);
    CHECK(output.find("INVALID menu selection") != std::string::npos);
    CHECK(output.find("INVALID train number") != std::string::npos);
    CHECK(output.find("OK: logged in (ref: admin)") != std::string::npos);
    CHECK(output.find("OK: logged out (ref: admin)") != std::string::npos);
    CHECK(err.str().empty());
}
