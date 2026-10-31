#include "persistence.hpp"
#include "atomic_journal.hpp"
#include "platform_shim.hpp"
#include "validation.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace rrs {
namespace {

void validate_record_size(std::size_t record_size) {
    if (record_size == 0) {
        throw std::invalid_argument("record_size must be greater than zero");
    }
    if (record_size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::invalid_argument("record_size is too large");
    }
}

void validate_record(std::size_t record_size, std::string_view record) {
    validate_record_size(record_size);
    const auto result = validate_length("record", record, record_size, record_size);
    if (!result) {
        throw std::invalid_argument("record length does not match record_size");
    }
}

std::size_t checked_offset(std::size_t record_size, std::size_t index) {
    if (index > std::numeric_limits<std::size_t>::max() / record_size) {
        throw std::out_of_range("record index offset overflows");
    }
    const auto offset = index * record_size;
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::out_of_range("record index offset is too large");
    }
    return offset;
}

void ensure_directory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("cannot create data directory: " + error.message());
    }
}

}  // namespace

PersistenceFileLock::PersistenceFileLock(const std::filesystem::path& lock_path)
    : lock_(std::make_unique<FileLock>(lock_path)) {}

PersistenceFileLock::~PersistenceFileLock() = default;
PersistenceFileLock::PersistenceFileLock(PersistenceFileLock&&) noexcept = default;
PersistenceFileLock& PersistenceFileLock::operator=(PersistenceFileLock&&) noexcept = default;

PersistenceStore::PersistenceStore(std::filesystem::path data_directory)
    : data_directory_(std::move(data_directory)) {
    if (data_directory_.empty()) {
        throw std::invalid_argument("data directory must not be empty");
    }
    ensure_directory(data_directory_);
    // Recover interrupted multi-file updates before any module reads persistent records.
    AtomicJournal journal(*this);
}

std::filesystem::path PersistenceStore::resolve_file(std::string_view file_name) const {
    const auto presence = validate_presence("file_name", file_name);
    if (!presence) {
        throw std::invalid_argument("file name must not be empty");
    }
    const std::filesystem::path supplied{std::string(file_name)};
    if (supplied.is_absolute() || supplied.has_parent_path() || supplied.filename() != supplied) {
        throw std::invalid_argument("file name must be a plain file name within the data directory");
    }
    if (supplied == "." || supplied == "..") {
        throw std::invalid_argument("invalid file name");
    }
    return data_directory_ / supplied;
}

std::optional<std::string> PersistenceStore::read_record(std::string_view file_name,
                                                          std::size_t record_size,
                                                          std::size_t index) const {
    validate_record_size(record_size);
    const auto file_path = resolve_file(file_name);
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        std::error_code error;
        const bool exists = std::filesystem::exists(file_path, error);
        if (!error && !exists) {
            return std::nullopt;
        }
        throw std::runtime_error("cannot open record file for reading: " + file_path.string());
    }

    input.seekg(0, std::ios::end);
    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error("cannot determine record file size: " + file_path.string());
    }
    const auto file_bytes = static_cast<std::uintmax_t>(end_position);
    if (file_bytes % record_size != 0) {
        throw std::runtime_error("record file contains a partial record: " + file_path.string());
    }

    const auto offset = checked_offset(record_size, index);
    if (static_cast<std::uintmax_t>(offset) >= file_bytes) {
        return std::nullopt;
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to record: " + file_path.string());
    }
    std::string record(record_size, '\0');
    input.read(record.data(), static_cast<std::streamsize>(record_size));
    if (input.gcount() != static_cast<std::streamsize>(record_size)) {
        throw std::runtime_error("short read from record file: " + file_path.string());
    }
    return record;
}

std::vector<std::string> PersistenceStore::read_records(std::string_view file_name,
                                                        std::size_t record_size,
                                                        std::optional<std::size_t> maximum_records) const {
    validate_record_size(record_size);
    if (record_size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::invalid_argument("record_size is too large");
    }
    const auto file_path = resolve_file(file_name);
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        std::error_code error;
        const bool exists = std::filesystem::exists(file_path, error);
        if (!error && !exists) {
            return {};
        }
        throw std::runtime_error("cannot open record file for reading: " + file_path.string());
    }

    input.seekg(0, std::ios::end);
    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error("cannot determine record file size: " + file_path.string());
    }
    const auto file_bytes = static_cast<std::uintmax_t>(end_position);
    if (file_bytes % record_size != 0) {
        throw std::runtime_error("record file contains a partial record: " + file_path.string());
    }
    if (file_bytes > static_cast<std::uintmax_t>(
                         std::numeric_limits<std::streamsize>::max()) ||
        file_bytes > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("record file is too large to read: " + file_path.string());
    }

    const auto count = static_cast<std::size_t>(file_bytes / record_size);
    if (maximum_records && count > *maximum_records) {
        throw std::length_error("record file exceeds the caller-supplied record limit");
    }
    std::vector<std::string> records;
    records.reserve(count);
    input.seekg(0, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to record file: " + file_path.string());
    }
    for (std::size_t index = 0; index < count; ++index) {
        std::string record(record_size, '\0');
        input.read(record.data(), static_cast<std::streamsize>(record_size));
        if (input.gcount() != static_cast<std::streamsize>(record_size)) {
            throw std::runtime_error("short read from record file: " + file_path.string());
        }
        records.push_back(std::move(record));
    }
    return records;
}

void PersistenceStore::write_record(std::string_view file_name, std::size_t record_size,
                                    std::size_t index, std::string_view record) const {
    validate_record(record_size, record);
    const auto file_path = resolve_file(file_name);
    ensure_directory(data_directory_);

    std::error_code error;
    const bool exists = std::filesystem::exists(file_path, error);
    if (error) {
        throw std::runtime_error("cannot inspect record file: " + error.message());
    }
    std::uintmax_t file_bytes = 0;
    if (exists) {
        file_bytes = std::filesystem::file_size(file_path, error);
        if (error) {
            throw std::runtime_error("cannot inspect record file size: " + error.message());
        }
        if (file_bytes % record_size != 0) {
            throw std::runtime_error("record file contains a partial record: " + file_path.string());
        }
    }

    const auto offset = checked_offset(record_size, index);
    const auto record_count = file_bytes / record_size;
    if (index > record_count) {
        throw std::out_of_range("cannot write past the end of a fixed-length record file");
    }

    std::fstream output(file_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!output && !exists && index == 0) {
        // Create a new file only for its first record; do not permit sparse record gaps.
        std::ofstream create(file_path, std::ios::binary | std::ios::out);
        if (!create) {
            throw std::runtime_error("cannot create record file: " + file_path.string());
        }
        create.close();
        output.open(file_path, std::ios::binary | std::ios::in | std::ios::out);
    }
    if (!output) {
        throw std::runtime_error("cannot open record file for writing: " + file_path.string());
    }
    output.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!output) {
        throw std::runtime_error("cannot seek to record for writing: " + file_path.string());
    }
    output.write(record.data(), static_cast<std::streamsize>(record_size));
    output.flush();
    if (!output) {
        throw std::runtime_error("failed writing record: " + file_path.string());
    }
}

std::size_t PersistenceStore::record_count(std::string_view file_name,
                                           std::size_t record_size) const {
    validate_record_size(record_size);
    const auto file_path = resolve_file(file_name);
    std::error_code error;
    const bool exists = std::filesystem::exists(file_path, error);
    if (error) {
        throw std::runtime_error("cannot inspect record file: " + error.message());
    }
    if (!exists) {
        return 0;
    }
    const auto bytes = std::filesystem::file_size(file_path, error);
    if (error) {
        throw std::runtime_error("cannot inspect record file size: " + error.message());
    }
    if (bytes % record_size != 0) {
        throw std::runtime_error("record file contains a partial record: " + file_path.string());
    }
    const auto count = bytes / record_size;
    if (count > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("record count exceeds size_t");
    }
    return static_cast<std::size_t>(count);
}

PersistenceFileLock PersistenceStore::lock_file(std::string_view file_name) const {
    auto lock_path = resolve_file(file_name);
    lock_path += ".lock";
    return PersistenceFileLock(lock_path);
}

const std::filesystem::path& PersistenceStore::data_directory() const noexcept {
    return data_directory_;
}

}  // namespace rrs
