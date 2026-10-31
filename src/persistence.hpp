#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rrs {

class FileLock;
class PersistenceStore;

// Cross-process exclusive lock for one persistent file. The stable sidecar lock identity is
// retained after release so separate processes always lock the same file. On POSIX, fcntl locks
// are process-associated, so callers must also serialize same-process operations for one file.
class PersistenceFileLock final {
public:
    ~PersistenceFileLock();
    PersistenceFileLock(PersistenceFileLock&&) noexcept;
    PersistenceFileLock& operator=(PersistenceFileLock&&) noexcept;
    PersistenceFileLock(const PersistenceFileLock&) = delete;
    PersistenceFileLock& operator=(const PersistenceFileLock&) = delete;

private:
    explicit PersistenceFileLock(const std::filesystem::path& lock_path);
    std::unique_ptr<FileLock> lock_;
    friend class PersistenceStore;
};

// M1 PersistenceModule: indexed fixed-length binary records in local files.
// Record layout is supplied by each caller; this module treats records as opaque bytes.
// This avoids silently imposing a schema different from the SRS and dependent modules.
class PersistenceStore {
public:
    explicit PersistenceStore(std::filesystem::path data_directory);

    // Reads the record at zero-based index. nullopt means the index is past EOF.
    // Invalid record size and I/O errors throw std::runtime_error.
    std::optional<std::string> read_record(std::string_view file_name,
                                           std::size_t record_size,
                                           std::size_t index) const;

    // Reads all fixed-size records in index order using one file open. Missing files are empty.
    // An optional generic bound is checked before allocation. Partial records and I/O errors
    // throw std::runtime_error; exceeding the caller-supplied bound throws std::length_error.
    std::vector<std::string> read_records(std::string_view file_name,
                                          std::size_t record_size,
                                          std::optional<std::size_t> maximum_records =
                                              std::nullopt) const;

    // Appends a new record at EOF, or replaces an existing record at index.
    // All records in the file must have exactly record_size bytes.
    void write_record(std::string_view file_name, std::size_t record_size,
                      std::size_t index, std::string_view record) const;

    // Number of complete records in a file. Missing files contain zero records.
    std::size_t record_count(std::string_view file_name, std::size_t record_size) const;

    // Acquires a cross-process exclusive lock on the stable sidecar `<file_name>.lock`.
    // On POSIX callers must additionally serialize same-process operations for the same file.
    // The returned RAII guard keeps the lock until destruction.
    PersistenceFileLock lock_file(std::string_view file_name) const;

    const std::filesystem::path& data_directory() const noexcept;

private:
    std::filesystem::path data_directory_;

    std::filesystem::path resolve_file(std::string_view file_name) const;
};

}  // namespace rrs
