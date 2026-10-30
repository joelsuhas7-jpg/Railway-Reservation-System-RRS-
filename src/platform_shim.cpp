#include "platform_shim.hpp"

#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace rrs {
namespace {

std::runtime_error system_failure(const char* operation, const std::filesystem::path& path) {
    return std::runtime_error(std::string(operation) + " failed for " + path.string() + ": " +
                              std::error_code(errno, std::generic_category()).message());
}

}  // namespace

FileLock::FileLock(const std::filesystem::path& path) {
#ifdef _WIN32
    // Win32 locking is intentionally a stub until the application targets the
    // Windows platform for concurrent writers.
    (void)path;
#else
    handle_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (handle_ == -1) {
        throw system_failure("open lock file", path);
    }
    while (::flock(handle_, LOCK_EX) == -1) {
        if (errno == EINTR) {
            continue;
        }
        const auto failure = system_failure("lock file", path);
        ::close(handle_);
        handle_ = -1;
        throw failure;
    }
#endif
}

FileLock::~FileLock() {
#ifndef _WIN32
    if (handle_ != -1) {
        (void)::flock(handle_, LOCK_UN);
        (void)::close(handle_);
    }
#endif
}

FileLock::FileLock(FileLock&& other) noexcept : handle_(other.handle_) {
    other.handle_ = -1;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
#ifndef _WIN32
        if (handle_ != -1) {
            (void)::flock(handle_, LOCK_UN);
            (void)::close(handle_);
        }
#endif
        handle_ = other.handle_;
        other.handle_ = -1;
    }
    return *this;
}

void sync_file(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
#else
    const int handle = ::open(path.c_str(), O_RDONLY);
    if (handle == -1) {
        throw system_failure("open file for sync", path);
    }
    const int result = ::fsync(handle);
    const int saved_errno = errno;
    (void)::close(handle);
    if (result == -1) {
        errno = saved_errno;
        throw system_failure("fsync", path);
    }
#endif
}

}  // namespace rrs
