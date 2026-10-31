#pragma once

#include <filesystem>

namespace rrs {

// Blocking advisory exclusive lock for a file. The lock is released when the
// object is destroyed. POSIX uses fcntl(F_SETLKW); Windows uses LockFileEx.
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
