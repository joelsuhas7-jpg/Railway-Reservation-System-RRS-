#include "atomic_journal.hpp"

#include "platform_shim.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rrs {
namespace {

constexpr char kMagic[] = "RRSJNL1";
constexpr std::size_t kMagicSize = sizeof(kMagic) - 1;
constexpr std::uint64_t kMaxJournalBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxWrites = 100000ULL;
const char* kJournalName = ".rrs-atomic-journal";
const char* kTemporaryName = ".rrs-atomic-journal.tmp";
const char* kLockName = ".rrs-atomic-journal.lock";

void append_u64(std::string& bytes, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

std::uint64_t read_u64(const std::string& bytes, std::size_t& offset) {
    if (bytes.size() - offset < 8) {
        throw std::runtime_error("truncated atomic journal");
    }
    std::uint64_t value = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(bytes[offset++])) << shift;
    }
    return value;
}

std::uint64_t checksum(std::string_view bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

std::string serialize(const std::vector<RecordWrite>& writes) {
    std::string bytes(kMagic, kMagicSize);
    append_u64(bytes, static_cast<std::uint64_t>(writes.size()));
    for (const auto& write : writes) {
        append_u64(bytes, static_cast<std::uint64_t>(write.file_name.size()));
        bytes += write.file_name;
        append_u64(bytes, static_cast<std::uint64_t>(write.record_size));
        append_u64(bytes, static_cast<std::uint64_t>(write.index));
        append_u64(bytes, static_cast<std::uint64_t>(write.record.size()));
        bytes += write.record;
        if (bytes.size() > kMaxJournalBytes - 8) {
            throw std::length_error("atomic journal exceeds maximum size");
        }
    }
    append_u64(bytes, checksum(bytes));
    return bytes;
}

std::vector<RecordWrite> deserialize(const std::string& bytes) {
    if (bytes.size() < kMagicSize + 16 || bytes.size() > kMaxJournalBytes ||
        bytes.compare(0, kMagicSize, kMagic, kMagicSize) != 0) {
        throw std::runtime_error("invalid atomic journal header or size");
    }
    const std::size_t checksum_offset = bytes.size() - 8;
    std::size_t checksum_cursor = checksum_offset;
    const auto stored_checksum = read_u64(bytes, checksum_cursor);
    if (stored_checksum != checksum(std::string_view(bytes).substr(0, checksum_offset))) {
        throw std::runtime_error("atomic journal checksum mismatch");
    }

    std::size_t offset = kMagicSize;
    const auto count = read_u64(bytes, offset);
    if (count == 0 || count > kMaxWrites) {
        throw std::runtime_error("invalid atomic journal write count");
    }
    std::vector<RecordWrite> writes;
    writes.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto name_size = read_u64(bytes, offset);
        if (name_size == 0 || name_size > checksum_offset - offset) {
            throw std::runtime_error("invalid atomic journal file name length");
        }
        RecordWrite write;
        write.file_name.assign(bytes.data() + offset, static_cast<std::size_t>(name_size));
        offset += static_cast<std::size_t>(name_size);
        const auto record_size = read_u64(bytes, offset);
        const auto index = read_u64(bytes, offset);
        const auto payload_size = read_u64(bytes, offset);
        if (record_size == 0 || record_size > std::numeric_limits<std::size_t>::max() ||
            index > std::numeric_limits<std::size_t>::max() ||
            payload_size != record_size || payload_size > checksum_offset - offset) {
            throw std::runtime_error("invalid atomic journal record");
        }
        write.record_size = static_cast<std::size_t>(record_size);
        write.index = static_cast<std::size_t>(index);
        write.record.assign(bytes.data() + offset, static_cast<std::size_t>(payload_size));
        offset += static_cast<std::size_t>(payload_size);
        writes.push_back(std::move(write));
    }
    if (offset != checksum_offset) {
        throw std::runtime_error("atomic journal contains trailing data");
    }
    return writes;
}

void sync_directory(const std::filesystem::path& directory) {
    sync_file(directory);
}

void validate_writes(PersistenceStore& store, const std::vector<RecordWrite>& writes) {
    if (writes.empty()) {
        throw std::invalid_argument("atomic journal transaction must contain at least one write");
    }
    if (writes.size() > kMaxWrites) {
        throw std::length_error("atomic journal transaction contains too many writes");
    }
    for (std::size_t i = 0; i < writes.size(); ++i) {
        const auto& write = writes[i];
        if (write.record_size == 0 || write.record.size() != write.record_size) {
            throw std::invalid_argument("atomic journal record size does not match payload");
        }
        const auto count = store.record_count(write.file_name, write.record_size);
        if (write.index > count) {
            throw std::out_of_range("atomic journal cannot write a sparse record index");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (writes[j].file_name == write.file_name &&
                writes[j].record_size == write.record_size &&
                writes[j].index == write.index) {
                throw std::invalid_argument("atomic journal contains duplicate record targets");
            }
        }
    }
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open atomic journal for reading");
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kMaxJournalBytes) {
        throw std::runtime_error("atomic journal has invalid size");
    }
    std::string bytes(static_cast<std::size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input && !bytes.empty()) {
        throw std::runtime_error("cannot read complete atomic journal");
    }
    return bytes;
}

void recover_unlocked(PersistenceStore& store) {
    const auto directory = store.data_directory();
    const auto journal = directory / kJournalName;
    std::error_code error;
    if (!std::filesystem::exists(journal, error)) {
        if (error) {
            throw std::runtime_error("cannot inspect atomic journal: " + error.message());
        }
        std::filesystem::remove(directory / kTemporaryName, error);
        return;
    }

    const auto writes = deserialize(read_all(journal));
    for (const auto& write : writes) {
        store.write_record(write.file_name, write.record_size, write.index, write.record);
        sync_file(directory / write.file_name);
    }
    std::filesystem::remove(journal, error);
    if (error) {
        throw std::runtime_error("cannot remove recovered atomic journal: " + error.message());
    }
    std::filesystem::remove(directory / kTemporaryName, error);
    if (error) {
        throw std::runtime_error("cannot remove stale atomic journal temporary file: " +
                                 error.message());
    }
    sync_directory(directory);
}

}  // namespace

AtomicJournal::AtomicJournal(PersistenceStore& store) : store_(store) {
    recover();
}

void AtomicJournal::commit(const std::vector<RecordWrite>& writes,
                           const CrashHook& crash_hook) {
    FileLock lock(store_.data_directory() / kLockName);
    recover_unlocked(store_);
    validate_writes(store_, writes);

    const auto directory = store_.data_directory();
    const auto temporary = directory / kTemporaryName;
    const auto journal = directory / kJournalName;
    const auto bytes = serialize(writes);

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create atomic journal temporary file");
        }
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error("cannot write complete atomic journal");
        }
    }
    sync_file(temporary);
    std::error_code error;
    std::filesystem::rename(temporary, journal, error);
    if (error) {
        throw std::runtime_error("cannot publish atomic journal: " + error.message());
    }
    sync_directory(directory);

    if (crash_hook) {
        crash_hook(JournalStage::journal_durable, 0);
    }
    for (std::size_t i = 0; i < writes.size(); ++i) {
        const auto& write = writes[i];
        store_.write_record(write.file_name, write.record_size, write.index, write.record);
        sync_file(directory / write.file_name);
        if (crash_hook) {
            crash_hook(JournalStage::record_applied, i + 1);
        }
    }

    std::filesystem::remove(journal, error);
    if (error) {
        throw std::runtime_error("cannot remove completed atomic journal: " + error.message());
    }
    sync_directory(directory);
}

void AtomicJournal::recover() {
    FileLock lock(store_.data_directory() / kLockName);
    recover_unlocked(store_);
}

}  // namespace rrs
