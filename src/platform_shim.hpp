#pragma once

#include <filesystem>

namespace rrs {

// Advisory exclusive lock for a file. The lock is released when the object is
// destroyed. On platforms without a supported locking implementation this is
// a documented no-op so the rest of the application remains portable.
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& path);
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;

private:
    int handle_ = -1;
};

// Flushes file contents to stable storage where the platform exposes fsync.
void sync_file(const std::filesystem::path& path);

}  // namespace rrs
