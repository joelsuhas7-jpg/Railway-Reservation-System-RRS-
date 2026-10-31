#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view kTimestamp = "2026-09-30T10:00:00Z";
constexpr std::string_view kWorkerTestName = "TC-FARE-01: hidden cross-process worker";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-fare-process-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create fare process directory",
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

std::string environment(const char* name) {
#ifdef _WIN32
    const auto wide_name = std::filesystem::path(name).wstring();
    const wchar_t* value = ::_wgetenv(wide_name.c_str());
    if (value == nullptr) {
        return {};
    }
    const int required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                                nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        throw std::runtime_error("cannot convert worker environment from UTF-16");
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(),
                              required, nullptr, nullptr) == 0) {
        throw std::runtime_error("cannot convert worker environment from UTF-16");
    }
    result.pop_back();
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

bool wait_for(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::yield();
    }
    return predicate();
}

std::filesystem::path current_executable() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        throw std::runtime_error("cannot determine test executable path");
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
#else
    std::vector<char> buffer(4096);
    const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length <= 0 || static_cast<std::size_t>(length) == buffer.size()) {
        throw std::runtime_error("cannot determine test executable path");
    }
    return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length)));
#endif
}

#ifdef _WIN32
std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                               static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("cannot convert worker environment to UTF-16");
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(), required) != required) {
        throw std::runtime_error("cannot convert worker environment to UTF-16");
    }
    return result;
}
#endif

#ifdef _WIN32
struct ChildProcess {
    PROCESS_INFORMATION info{};

    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept : info(other.info) { other.info = {}; }

    DWORD wait() {
        if (::WaitForSingleObject(info.hProcess, 30000) != WAIT_OBJECT_0) {
            (void)::TerminateProcess(info.hProcess, 124);
            (void)::WaitForSingleObject(info.hProcess, INFINITE);
            ::CloseHandle(info.hThread);
            ::CloseHandle(info.hProcess);
            info = {};
            throw std::runtime_error("fare worker process timed out");
        }
        DWORD code = 1;
        (void)::GetExitCodeProcess(info.hProcess, &code);
        ::CloseHandle(info.hThread);
        ::CloseHandle(info.hProcess);
        info = {};
        return code;
    }

    ~ChildProcess() {
        if (info.hProcess != nullptr) {
            (void)::TerminateProcess(info.hProcess, 125);
            (void)::WaitForSingleObject(info.hProcess, INFINITE);
            ::CloseHandle(info.hThread);
            ::CloseHandle(info.hProcess);
        }
    }
};

ChildProcess launch_worker(const std::map<std::string, std::string>& values) {
    std::map<std::wstring, std::wstring> overrides;
    for (const auto& [key, value] : values) {
        overrides.emplace(widen(key), widen(value));
    }
    std::vector<std::wstring> entries;
    LPWCH block = ::GetEnvironmentStringsW();
    if (block == nullptr) {
        throw std::runtime_error("cannot read process environment");
    }
    for (const wchar_t* current = block; *current != L'\0';) {
        std::wstring entry(current);
        current += entry.size() + 1;
        const auto separator = entry.find(L'=');
        if (separator != std::wstring::npos && separator != 0) {
            const auto key = entry.substr(0, separator);
            const bool replaced = std::any_of(overrides.begin(), overrides.end(),
                                              [&](const auto& item) {
                                                  return _wcsicmp(item.first.c_str(),
                                                                  key.c_str()) == 0;
                                              });
            if (!replaced) {
                entries.push_back(std::move(entry));
            }
        } else {
            entries.push_back(std::move(entry));
        }
    }
    ::FreeEnvironmentStringsW(block);
    for (const auto& [key, value] : overrides) {
        entries.push_back(key + L"=" + value);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    std::vector<wchar_t> environment_block;
    for (const auto& entry : entries) {
        environment_block.insert(environment_block.end(), entry.begin(), entry.end());
        environment_block.push_back(L'\0');
    }
    environment_block.push_back(L'\0');

    const auto executable = current_executable();
    std::wstring command = L"\"" + executable.wstring() + L"\" \"" +
                           std::wstring(kWorkerTestName.begin(), kWorkerTestName.end()) +
                           L"\" --reporter compact";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    ChildProcess child;
    if (!::CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                          CREATE_UNICODE_ENVIRONMENT, environment_block.data(), nullptr,
                          &startup, &child.info)) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                "cannot start fare worker process");
    }
    return child;
}
#else
struct ChildProcess {
    pid_t pid{-1};
    ChildProcess() = default;
    explicit ChildProcess(pid_t child) : pid(child) {}
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept : pid(other.pid) { other.pid = -1; }
    int wait() {
        int status = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = ::waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                pid = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
            }
            if (result == -1) {
                throw std::system_error(errno, std::generic_category(), "waitpid failed");
            }
            std::this_thread::yield();
        }
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, &status, 0);
        pid = -1;
        throw std::runtime_error("fare worker process timed out");
    }
    ~ChildProcess() {
        if (pid > 0) {
            (void)::kill(pid, SIGKILL);
            (void)::waitpid(pid, nullptr, 0);
        }
    }
};

ChildProcess launch_worker(const std::map<std::string, std::string>& values) {
    const auto executable = current_executable();
    const pid_t child = ::fork();
    if (child < 0) {
        throw std::system_error(errno, std::generic_category(), "fork failed");
    }
    if (child == 0) {
        for (const auto& [key, value] : values) {
            (void)::setenv(key.c_str(), value.c_str(), 1);
        }
        ::execl(executable.c_str(), executable.c_str(),
                "TC-FARE-01: hidden cross-process worker", "--reporter", "compact",
                static_cast<char*>(nullptr));
        ::_exit(126);
    }
    return ChildProcess(child);
}
#endif

std::map<std::string, std::string> worker_environment(
    std::string_view mode, const std::filesystem::path& directory,
    const std::filesystem::path& ready, const std::filesystem::path& go,
    const std::filesystem::path& result, std::string_view fare,
    std::string_view date) {
    return {{"RRS_FARE_WORKER_MODE", std::string(mode)},
            {"RRS_FARE_WORKER_DIR", directory.u8string()},
            {"RRS_FARE_WORKER_READY", ready.u8string()},
            {"RRS_FARE_WORKER_GO", go.u8string()},
            {"RRS_FARE_WORKER_RESULT", result.u8string()},
            {"RRS_FARE_WORKER_VALUE", std::string(fare)},
            {"RRS_FARE_WORKER_DATE", std::string(date)}};
}

void touch(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create worker coordination file");
    }
}

std::pair<std::string, std::string> read_result(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string status;
    std::string message;
    std::getline(input, status);
    std::getline(input, message);
    return {status, message};
}

void seed_train_and_class(const std::filesystem::path& path) {
    rrs::PersistenceStore store(path);
    const rrs::AuditLog audit(path / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const rrs::TrainRecord train{12345, "Karnataka Express", "SBC", "NDLS",
                                 "06:00", "14:00", 0x7f};
    REQUIRE(trains.add_train(admin, kTimestamp, train).kind == rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    REQUIRE(classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
}

void seed_fare(const std::filesystem::path& path, std::int64_t amount,
               std::string_view date) {
    rrs::PersistenceStore store(path);
    const rrs::AuditLog audit(path / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(fares.configure_fare(admin, 12345, "SL", amount, date).kind ==
            rrs::OutcomeKind::Success);
}

void launch_pair(const std::filesystem::path& directory,
                 const std::map<std::string, std::string>& first,
                 const std::map<std::string, std::string>& second,
                 const std::filesystem::path& ready_first,
                 const std::filesystem::path& ready_second,
                 const std::filesystem::path& go) {
    auto child_one = launch_worker(first);
    auto child_two = launch_worker(second);
    const bool ready = wait_for([&] {
        return std::filesystem::exists(ready_first) &&
               std::filesystem::exists(ready_second);
    });
    touch(go);
    const auto first_exit = child_one.wait();
    const auto second_exit = child_two.wait();
    REQUIRE(ready);
    REQUIRE(first_exit == 0);
    REQUIRE(second_exit == 0);
    (void)directory;
}

}  // namespace

TEST_CASE("TC-FARE-01: hidden cross-process worker", "[.]" ) {
    const auto mode = environment("RRS_FARE_WORKER_MODE");
    if (mode.empty()) {
        return;
    }
    const std::filesystem::path directory =
        std::filesystem::u8path(environment("RRS_FARE_WORKER_DIR"));
    const std::filesystem::path ready =
        std::filesystem::u8path(environment("RRS_FARE_WORKER_READY"));
    const std::filesystem::path go =
        std::filesystem::u8path(environment("RRS_FARE_WORKER_GO"));
    const std::filesystem::path result_path =
        std::filesystem::u8path(environment("RRS_FARE_WORKER_RESULT"));
    const auto value = environment("RRS_FARE_WORKER_VALUE");
    const auto date = environment("RRS_FARE_WORKER_DATE");
    rrs::PersistenceStore store(directory);
    const rrs::AuditLog audit(directory / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    touch(ready);
    if (!wait_for([&] { return std::filesystem::exists(go); })) {
        FAIL("parent did not release the concurrent fare workers");
    }

    rrs::Outcome outcome;
    if (mode == "append") {
        outcome = fares.configure_fare(admin, 12345, "SL", std::stoll(value), date);
    } else {
        const auto expected = std::stoll(environment("RRS_FARE_WORKER_EXPECTED"));
        const auto replacement = environment("RRS_FARE_WORKER_NEW_VALUE");
        outcome = fares.replace_fare(admin, 12345, "SL", "2026-10-01", 1, expected,
                                     std::stoll(replacement), "2026-10-01");
    }
    std::ofstream result(result_path, std::ios::binary);
    result << static_cast<int>(outcome.kind) << '\n' << outcome.message << '\n';
}

TEST_CASE("TC-FARE-01: separate processes preserve distinct simultaneous fare appends",
          "[integration][M1][M3][M7]") {
    TempDirectory temp;
    seed_train_and_class(temp.path());
    {
        std::ofstream empty(temp.path() / std::string(rrs::kFaresFile), std::ios::binary);
    }
    const auto ready_a = temp.path() / "ready-a";
    const auto ready_b = temp.path() / "ready-b";
    const auto result_a = temp.path() / "result-a";
    const auto result_b = temp.path() / "result-b";
    const auto go = temp.path() / "go";
    const auto first = worker_environment("append", temp.path(), ready_a, go, result_a,
                                          "45000", "2026-10-01");
    const auto second = worker_environment("append", temp.path(), ready_b, go, result_b,
                                           "50000", "2026-10-10");
    launch_pair(temp.path(), first, second, ready_a, ready_b, go);
    CHECK(read_result(result_a).first == "0");
    CHECK(read_result(result_b).first == "0");

    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });
    const auto records = fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    CHECK(std::any_of(records.begin(), records.end(), [](const auto& record) {
        return record.fare_paise == 45000 && record.effective_from == "2026-10-01";
    }));
    CHECK(std::any_of(records.begin(), records.end(), [](const auto& record) {
        return record.fare_paise == 50000 && record.effective_from == "2026-10-10";
    }));
    std::ifstream audit_input(temp.path() / "audit.log");
    std::vector<std::string> fare_audits;
    for (std::string line; std::getline(audit_input, line);) {
        if (line.find("\tFARE_CONFIGURED\t") != std::string::npos) {
            fare_audits.push_back(std::move(line));
        }
    }
    REQUIRE(fare_audits.size() == 2);
    CHECK(std::find(fare_audits.begin(), fare_audits.end(),
                    "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_CONFIGURED\t12345:SL:2026-10-01") !=
          fare_audits.end());
    CHECK(std::find(fare_audits.begin(), fare_audits.end(),
                    "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_CONFIGURED\t12345:SL:2026-10-10") !=
          fare_audits.end());
}

TEST_CASE("TC-FARE-01: separate processes serialize duplicate fare appends",
          "[integration][M1][M3][M7]") {
    TempDirectory temp;
    seed_train_and_class(temp.path());
    const auto ready_a = temp.path() / "ready-a";
    const auto ready_b = temp.path() / "ready-b";
    const auto result_a = temp.path() / "result-a";
    const auto result_b = temp.path() / "result-b";
    const auto go = temp.path() / "go";
    const auto first = worker_environment("append", temp.path(), ready_a, go, result_a,
                                          "45000", "2026-10-01");
    const auto second = worker_environment("append", temp.path(), ready_b, go, result_b,
                                           "50000", "2026-10-01");
    launch_pair(temp.path(), first, second, ready_a, ready_b, go);
    const auto first_result = read_result(result_a);
    const auto second_result = read_result(result_b);
    const int successes = (first_result.first == "0" ? 1 : 0) +
                          (second_result.first == "0" ? 1 : 0);
    CHECK(successes == 1);
    CHECK((first_result.first == "2" || second_result.first == "2"));
    CHECK((first_result.second.find("already exists") != std::string::npos ||
           second_result.second.find("already exists") != std::string::npos));

    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });
    const auto records = fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 1);
    CHECK(records.front().effective_from == "2026-10-01");
    std::ifstream input(temp.path() / "audit.log");
    std::vector<std::string> fare_configured;
    for (std::string line; std::getline(input, line);) {
        if (line.find("\tFARE_CONFIGURED\t") != std::string::npos) {
            fare_configured.push_back(std::move(line));
        }
    }
    REQUIRE(fare_configured.size() == 1);
    CHECK(fare_configured.front() ==
          "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_CONFIGURED\t12345:SL:2026-10-01");
}

TEST_CASE("TC-FARE-01: stale concurrent replacement refuses without changing fare history",
          "[integration][M1][M3][M7]") {
    TempDirectory temp;
    seed_train_and_class(temp.path());
    seed_fare(temp.path(), 45000, "2026-10-01");
    seed_fare(temp.path(), 50000, "2026-10-10");
    const auto ready_a = temp.path() / "ready-a";
    const auto ready_b = temp.path() / "ready-b";
    const auto result_a = temp.path() / "result-a";
    const auto result_b = temp.path() / "result-b";
    const auto go = temp.path() / "go";
    auto first = worker_environment("replace", temp.path(), ready_a, go, result_a,
                                    "0", "2026-10-01");
    auto second = worker_environment("replace", temp.path(), ready_b, go, result_b,
                                     "0", "2026-10-01");
    first.emplace("RRS_FARE_WORKER_EXPECTED", "45000");
    first.emplace("RRS_FARE_WORKER_NEW_VALUE", "61000");
    second.emplace("RRS_FARE_WORKER_EXPECTED", "45000");
    second.emplace("RRS_FARE_WORKER_NEW_VALUE", "62000");
    launch_pair(temp.path(), first, second, ready_a, ready_b, go);
    const auto first_result = read_result(result_a);
    const auto second_result = read_result(result_b);
    const int successes = (first_result.first == "0" ? 1 : 0) +
                          (second_result.first == "0" ? 1 : 0);
    CHECK(successes == 1);
    CHECK((first_result.first == "2" || second_result.first == "2"));
    CHECK((first_result.second.find("changed") != std::string::npos ||
           second_result.second.find("changed") != std::string::npos));

    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });
    const auto records = fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    CHECK((records[0].fare_paise == 61000 || records[0].fare_paise == 62000));
    CHECK(records[0].effective_from == "2026-10-01");
    CHECK(records[1].fare_paise == 50000);
    CHECK(records[1].effective_from == "2026-10-10");
    std::ifstream input(temp.path() / "audit.log");
    int fare_updated = 0;
    for (std::string line; std::getline(input, line);) {
        if (line.find("\tFARE_UPDATED\t12345:SL:2026-10-01") != std::string::npos) {
            ++fare_updated;
        }
    }
    CHECK(fare_updated == 1);
}
