#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "atomic_journal.hpp"
#include "audit_log.hpp"
#include "authentication.hpp"
#include "console_ui.hpp"
#include "persistence.hpp"

namespace rrs {

inline constexpr std::string_view kTrainsFile = "trains.dat";
inline constexpr std::size_t kTrainRecordSize = 128;
inline constexpr std::size_t kMaximumTrainRecords = 2000;

// Seven-day schedule. Bit 0 is Monday; bit 6 is Sunday.
struct TrainRecord {
    std::uint32_t train_number{0};
    std::string name;
    std::string source_station;
    std::string destination_station;
    std::string departure_time;
    std::string arrival_time;
    std::uint8_t running_days_mask{0};
};

// M6 TrainCatalogueModule: manages train and schedule master data in trains.dat.
// A module instance owns a PersistenceStore copy and its journal; reads always consult disk so
// successful changes are immediately visible to other catalogue reads in the same process.
class TrainCatalogueModule final {
public:
    TrainCatalogueModule(PersistenceStore store, const AuditLog& audit_log);

    TrainCatalogueModule(const TrainCatalogueModule&) = delete;
    TrainCatalogueModule& operator=(const TrainCatalogueModule&) = delete;
    TrainCatalogueModule(TrainCatalogueModule&&) = delete;
    TrainCatalogueModule& operator=(TrainCatalogueModule&&) = delete;

    Outcome add_train(const Session& session, std::string_view timestamp,
                      const TrainRecord& train);
    Outcome update_train(const Session& session, std::string_view timestamp,
                         std::uint32_t train_number, const TrainRecord& train);
    Outcome remove_train(const Session& session, std::string_view timestamp,
                         std::uint32_t train_number);

    // Corrupt, partial, unsupported, over-capacity, or duplicate data throws std::runtime_error.
    std::optional<TrainRecord> find_train(std::uint32_t train_number) const;
    std::vector<TrainRecord> list_active_trains() const;

private:
    struct StoredTrain {
        TrainRecord train;
        bool active{true};
    };

    std::vector<StoredTrain> read_all() const;
    std::optional<std::size_t> find_index(const std::vector<StoredTrain>& trains,
                                          std::uint32_t train_number) const;
    std::optional<std::string> append_success_audit(const Session& session,
                                                    std::string_view timestamp,
                                                    std::string_view action,
                                                    std::uint32_t train_number) const;

    PersistenceStore store_;
    std::unique_ptr<AtomicJournal> journal_;
    const AuditLog& audit_log_;
};

}  // namespace rrs
