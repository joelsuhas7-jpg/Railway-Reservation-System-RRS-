#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "persistence.hpp"

namespace rrs {

struct RecordWrite {
    std::string file_name;
    std::size_t record_size;
    std::size_t index;
    std::string record;
};

enum class JournalStage {
    journal_durable,
    record_applied
};

// Write-ahead journal for atomic updates spanning multiple fixed-record files.
// A pending journal is replayed on construction before callers can use the store.
class AtomicJournal final {
public:
    using CrashHook = std::function<void(JournalStage, std::size_t)>;

    explicit AtomicJournal(PersistenceStore& store);

    void commit(const std::vector<RecordWrite>& writes,
                const CrashHook& crash_hook = {});

    void recover();

private:
    PersistenceStore& store_;
};

}  // namespace rrs
