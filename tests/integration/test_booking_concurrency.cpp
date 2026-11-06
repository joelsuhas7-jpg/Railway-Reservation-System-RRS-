#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "audit_log.hpp"
#include "booking.hpp"
#include "class_fare_config.hpp"
#include "fare_calculation.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// TC-CONC-01 (RRS-F-016) and TC-PERF-03 (RRS-NF-SPD-003). Separate processes book against one
// data directory, so the advisory lock on the seat inventory is exercised across processes.
// The workers are forked children; those cases are not built on Windows, where fork() is absent.

namespace {

constexpr std::string_view kSetupTime = "2026-10-30T10:00:00Z";
constexpr std::string_view kJourneyDate = "2026-11-02";
constexpr std::uint32_t kTrain = 12345;
constexpr std::uint32_t kCapacity = 72;
constexpr double kCriticalSectionLimitMs = 100.0;

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-booking-conc-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create concurrency directory",
                                                        path_, error);
            }
        }
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// The M6 to M13 modules over one data directory, as one running application instance holds them.
struct Instance {
    explicit Instance(const std::filesystem::path& directory, rrs::PnrGenerator pnrs = {})
        : store{directory},
          audit{directory / "audit.log"},
          trains{store, audit},
          classes{store, trains, audit, [] { return std::string(kSetupTime); }},
          fares{store, trains, classes, audit, [] { return std::string(kSetupTime); }},
          inventory{store, classes, audit, [] { return std::string(kSetupTime); }},
          calculation{fares},
          booking{store, calculation, inventory, audit, std::move(pnrs)} {}

    rrs::SeatInventorySnapshot sl() {
        return inventory.get_inventory(kTrain, "SL", kJourneyDate);
    }

    rrs::PersistenceStore store;
    rrs::AuditLog audit;
    rrs::TrainCatalogueModule trains;
    rrs::ClassFareConfigModule classes;
    rrs::FareConfigModule fares;
    rrs::SeatInventoryModule inventory;
    rrs::FareCalculationModule calculation;
    rrs::BookingModule booking;
};

// Train 12345, SL(72) at 45000 paise, with `blocked` seats withheld for the journey date.
void seed(const std::filesystem::path& directory, std::uint32_t blocked) {
    Instance instance{directory};
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(instance.trains
                .add_train(admin, kSetupTime,
                           {kTrain, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f})
                .kind == rrs::OutcomeKind::Success);
    REQUIRE(instance.classes.add_class(admin, kSetupTime, kTrain, "SL", kCapacity).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(instance.fares.configure_fare(admin, kTrain, "SL", 45000, "2026-10-30").kind ==
            rrs::OutcomeKind::Success);
    if (blocked > 0) {
        REQUIRE(instance.inventory.block_seats(admin, kSetupTime, kTrain, "SL", kJourneyDate,
                                               blocked)
                    .kind == rrs::OutcomeKind::Success);
    }
}

rrs::BookingRequest one_passenger_request(const std::string& reference) {
    rrs::BookingRequest request;
    request.train_number = kTrain;
    request.class_code = "SL";
    request.journey_date = std::string(kJourneyDate);
    request.passengers = {{"Asha Rao", 34, rrs::Gender::Female}};
    request.payment = {rrs::PaymentOutcome::success, reference};
    return request;
}

double p95(std::vector<double> samples) {
    REQUIRE_FALSE(samples.empty());
    std::sort(samples.begin(), samples.end());
    const auto rank = (samples.size() * 95 + 99) / 100;  // nearest-rank, ceil(0.95 n)
    return samples[rank - 1];
}

// Times one booking through the critical section in milliseconds.
double timed_book(Instance& instance, const std::string& reference, rrs::Outcome& outcome) {
    const auto start = std::chrono::steady_clock::now();
    outcome = instance.booking.book({"passenger1", rrs::Role::Passenger}, kSetupTime,
                                    one_passenger_request(reference));
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(stop - start).count();
}

#ifndef _WIN32
// Distinct per-worker PNRs, so a PNR collision can never be mistaken for a lost booking.
rrs::PnrGenerator worker_pnrs(int worker) {
    auto next = std::make_shared<int>(0);
    return [worker, next] {
        auto pnr = "W" + std::to_string(worker) + "P" + std::to_string((*next)++);
        pnr.resize(rrs::kPnrLength, '0');
        return pnr;
    };
}

void touch(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create worker coordination file");
    }
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

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }
    return lines;
}

int count_audit(const std::filesystem::path& directory, std::string_view action) {
    const auto needle = "\t" + std::string(action) + "\t";
    int count = 0;
    for (const auto& line : read_lines(directory / "audit.log")) {
        if (line.find(needle) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

// One forked application instance. It opens its own modules, waits for the start signal and
// makes `bookings` one-passenger bookings. Each result line is "<kind>\t<ms>\t<detail>".
class Worker {
public:
    Worker(const std::filesystem::path& directory, int id, int bookings,
           const std::filesystem::path& go)
        : ready_(directory / ("ready-" + std::to_string(id))),
          result_(directory / ("result-" + std::to_string(id))) {
        std::cout.flush();
        std::cerr.flush();
        pid_ = ::fork();
        if (pid_ < 0) {
            throw std::system_error(errno, std::generic_category(), "fork failed");
        }
        if (pid_ == 0) {
            // The child never returns into Catch2 and skips every destructor of the parent.
            int code = 0;
            try {
                Instance instance{directory, worker_pnrs(id)};
                touch(ready_);
                if (!wait_for([&] { return std::filesystem::exists(go); })) {
                    ::_exit(3);
                }
                std::ofstream out(result_, std::ios::binary);
                for (int i = 0; i < bookings; ++i) {
                    rrs::Outcome outcome;
                    const auto ms = timed_book(instance,
                                               "PAY-W" + std::to_string(id) + "-" +
                                                   std::to_string(i),
                                               outcome);
                    out << static_cast<int>(outcome.kind) << '\t' << ms << '\t'
                        << (outcome.kind == rrs::OutcomeKind::Success ? outcome.detail
                                                                       : outcome.message)
                        << '\n';
                }
                out.flush();
                code = out ? 0 : 4;
            } catch (...) {
                code = 2;
            }
            ::_exit(code);
        }
    }
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&& other) noexcept
        : ready_(std::move(other.ready_)), result_(std::move(other.result_)), pid_(other.pid_) {
        other.pid_ = -1;
    }
    ~Worker() {
        if (pid_ > 0) {
            (void)::kill(pid_, SIGKILL);
            (void)::waitpid(pid_, nullptr, 0);
        }
    }

    bool ready() const { return std::filesystem::exists(ready_); }

    int wait() {
        int status = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = ::waitpid(pid_, &status, WNOHANG);
            if (result == pid_) {
                pid_ = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
            }
            if (result == -1) {
                throw std::system_error(errno, std::generic_category(), "waitpid failed");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("booking worker process timed out");
    }

    struct Line {
        int kind;
        double ms;
        std::string detail;
    };
    std::vector<Line> results() const {
        std::vector<Line> lines;
        for (const auto& text : read_lines(result_)) {
            const auto first = text.find('\t');
            const auto second = text.find('\t', first + 1);
            lines.push_back({std::stoi(text.substr(0, first)),
                             std::stod(text.substr(first + 1, second - first - 1)),
                             text.substr(second + 1)});
        }
        return lines;
    }

private:
    std::filesystem::path ready_;
    std::filesystem::path result_;
    pid_t pid_{-1};
};

// Starts `instances` workers, releases them together and returns them after they exit cleanly.
std::vector<Worker> run_workers(const std::filesystem::path& directory, int instances,
                                int bookings_each) {
    const auto go = directory / "go";
    std::vector<Worker> workers;
    workers.reserve(static_cast<std::size_t>(instances));
    for (int id = 0; id < instances; ++id) {
        workers.emplace_back(directory, id, bookings_each, go);
    }
    const bool ready = wait_for([&] {
        return std::all_of(workers.begin(), workers.end(),
                           [](const Worker& worker) { return worker.ready(); });
    });
    touch(go);
    std::vector<int> exits;
    for (auto& worker : workers) {
        exits.push_back(worker.wait());
    }
    REQUIRE(ready);
    for (const auto code : exits) {
        REQUIRE(code == 0);
    }
    return workers;
}
#endif

}  // namespace

#ifndef _WIN32
TEST_CASE("TC-CONC-01: two processes racing for the last seat confirm exactly one booking",
          "[integration][M8][M13]") {
    // Repeated rounds raise the chance that the two attempts overlap inside the lock window.
    for (int round = 0; round < 10; ++round) {
        INFO("round " << round);
        TempDirectory temp;
        seed(temp.path(), kCapacity - 1);
        {
            Instance before{temp.path()};
            REQUIRE(before.sl().free_seats == 1);
        }

        const auto workers = run_workers(temp.path(), 2, 1);
        int confirmed = 0;
        int refused = 0;
        for (const auto& worker : workers) {
            const auto lines = worker.results();
            REQUIRE(lines.size() == 1);
            if (lines.front().kind == static_cast<int>(rrs::OutcomeKind::Success)) {
                ++confirmed;
            } else {
                CHECK(lines.front().kind ==
                      static_cast<int>(rrs::OutcomeKind::BusinessRuleRefusal));
                CHECK(lines.front().detail == "insufficient seats: 1 requested, 0 available");
                ++refused;
            }
        }
        CHECK(confirmed == 1);
        CHECK(refused == 1);

        Instance after{temp.path()};
        const auto inventory = after.sl();
        CHECK(inventory.free_seats == 0);
        CHECK(inventory.allocated_seats == 1);
        CHECK(inventory.blocked_seats + inventory.allocated_seats <= inventory.total_seats);
        const auto bookings = after.booking.list_bookings();
        REQUIRE(bookings.size() == 1);
        CHECK(bookings.front().status == rrs::BookingStatus::Confirmed);
        CHECK(bookings.front().seat_numbers == std::vector<std::uint32_t>{1});
        CHECK(count_audit(temp.path(), "BOOKING_CONFIRMED") == 1);
    }
}

TEST_CASE("TC-CONC-01: four processes never oversell a class beyond its free seats",
          "[integration][M8][M13]") {
    TempDirectory temp;
    seed(temp.path(), kCapacity - 10);  // 10 free seats, 4 x 5 = 20 attempts
    const auto workers = run_workers(temp.path(), 4, 5);

    int confirmed = 0;
    for (const auto& worker : workers) {
        for (const auto& line : worker.results()) {
            if (line.kind == static_cast<int>(rrs::OutcomeKind::Success)) {
                ++confirmed;
            } else {
                CHECK(line.kind == static_cast<int>(rrs::OutcomeKind::BusinessRuleRefusal));
                CHECK(line.detail.rfind("insufficient seats:", 0) == 0);
            }
        }
    }
    CHECK(confirmed == 10);

    Instance after{temp.path()};
    const auto inventory = after.sl();
    CHECK(inventory.free_seats == 0);
    CHECK(inventory.allocated_seats == 10);
    const auto bookings = after.booking.list_bookings();
    REQUIRE(bookings.size() == 10);
    std::set<std::uint32_t> seats;
    std::set<std::string> pnrs;
    for (const auto& booking : bookings) {
        CHECK(booking.status == rrs::BookingStatus::Confirmed);
        seats.insert(booking.seat_numbers.begin(), booking.seat_numbers.end());
        pnrs.insert(booking.pnr);
    }
    CHECK(seats.size() == 10);  // no seat number sold twice
    CHECK(pnrs.size() == 10);
    CHECK(count_audit(temp.path(), "BOOKING_CONFIRMED") == 10);
}
#endif

TEST_CASE("TC-PERF-03: single-instance booking critical section p95 is within 100 ms",
          "[integration][perf][M8][M13]") {
    TempDirectory temp;
    seed(temp.path(), 0);
    Instance instance{temp.path()};
    std::vector<double> samples;
    for (std::uint32_t i = 0; i < 60; ++i) {
        rrs::Outcome outcome;
        samples.push_back(timed_book(instance, "PAY-PERF-" + std::to_string(i), outcome));
        REQUIRE(outcome.kind == rrs::OutcomeKind::Success);
    }
    const auto measured = p95(samples);
    std::cout << "TC-PERF-03 single instance: 60 bookings, p95 " << measured << " ms, max "
              << *std::max_element(samples.begin(), samples.end()) << " ms\n";
    CHECK(measured <= kCriticalSectionLimitMs);
    CHECK(instance.sl().allocated_seats == 60);
}

#ifndef _WIN32
TEST_CASE("TC-PERF-03: booking latency is reported under 2 and 4 instance contention",
          "[integration][perf][M8][M13]") {
    // Contention figures are reported for RRS-NF-SPD-003; correctness is asserted, latency
    // under contention includes lock waiting and is not held to the single-instance limit.
    for (const int instances : {2, 4}) {
        INFO(instances << " instances");
        TempDirectory temp;
        seed(temp.path(), 0);
        const int each = 60 / instances;
        const auto workers = run_workers(temp.path(), instances, each);
        std::vector<double> samples;
        for (const auto& worker : workers) {
            for (const auto& line : worker.results()) {
                CHECK(line.kind == static_cast<int>(rrs::OutcomeKind::Success));
                samples.push_back(line.ms);
            }
        }
        REQUIRE(samples.size() == 60);
        std::cout << "TC-PERF-03 " << instances << " instances: 60 bookings, p95 "
                  << p95(samples) << " ms, max "
                  << *std::max_element(samples.begin(), samples.end()) << " ms\n";
        Instance after{temp.path()};
        CHECK(after.sl().allocated_seats == 60);
        CHECK(after.booking.list_bookings().size() == 60);
    }
}
#endif
