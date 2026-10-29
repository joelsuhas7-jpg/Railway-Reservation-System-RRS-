#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rrs {

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

    // Appends a new record at EOF, or replaces an existing record at index.
    // All records in the file must have exactly record_size bytes.
    void write_record(std::string_view file_name, std::size_t record_size,
                      std::size_t index, std::string_view record) const;

    // Number of complete records in a file. Missing files contain zero records.
    std::size_t record_count(std::string_view file_name, std::size_t record_size) const;

    const std::filesystem::path& data_directory() const noexcept;

private:
    std::filesystem::path data_directory_;

    std::filesystem::path resolve_file(std::string_view file_name) const;
};

}  // namespace rrs
