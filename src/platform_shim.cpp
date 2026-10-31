#include "platform_shim.hpp"

#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cwchar>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace rrs {
namespace {

std::runtime_error system_failure(const char* operation, const std::filesystem::path& path) {
    return std::runtime_error(std::string(operation) + " failed for " + path.string() + ": " +
                              std::error_code(errno, std::generic_category()).message());
}

#ifdef _WIN32
std::runtime_error windows_failure(const char* operation,
                                   const std::filesystem::path& path,
                                   DWORD error) {
    return std::runtime_error(std::string(operation) + " failed for " + path.string() + ": " +
                              std::system_error(static_cast<int>(error),
                                                std::system_category()).what());
}

HANDLE native_handle(int descriptor, const std::filesystem::path& path) {
    const auto handle = ::_get_osfhandle(descriptor);
    if (handle == -1) {
        throw windows_failure("get lock handle", path, ::GetLastError());
    }
    return reinterpret_cast<HANDLE>(handle);
}
#endif

void release_lock(int& handle) noexcept {
    if (handle == -1) {
        return;
    }
#ifdef _WIN32
    OVERLAPPED overlapped{};
    const auto native = reinterpret_cast<HANDLE>(::_get_osfhandle(handle));
    if (native != INVALID_HANDLE_VALUE && native != nullptr) {
        (void)::UnlockFileEx(native, 0, MAXDWORD, MAXDWORD, &overlapped);
    }
    (void)::_close(handle);
#else
    struct flock lock_request {};
    lock_request.l_type = F_UNLCK;
    lock_request.l_whence = SEEK_SET;
    lock_request.l_start = 0;
    lock_request.l_len = 0;
    (void)::fcntl(handle, F_SETLK, &lock_request);
    (void)::close(handle);
#endif
    handle = -1;
}

}  // namespace

FileLock::FileLock(const std::filesystem::path& path) {
#ifdef _WIN32
    handle_ = ::_wopen(path.c_str(), _O_CREAT | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (handle_ == -1) {
        throw system_failure("open lock file", path);
    }
    OVERLAPPED overlapped{};
    HANDLE lock_handle = nullptr;
    try {
        lock_handle = native_handle(handle_, path);
    } catch (...) {
        (void)::_close(handle_);
        handle_ = -1;
        throw;
    }
    if (!::LockFileEx(lock_handle, LOCKFILE_EXCLUSIVE_LOCK, 0,
                      MAXDWORD, MAXDWORD, &overlapped)) {
        const auto failure = windows_failure("lock file", path, ::GetLastError());
        (void)::_close(handle_);
        handle_ = -1;
        throw failure;
    }
#else
    handle_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (handle_ == -1) {
        throw system_failure("open lock file", path);
    }
    struct flock lock_request {};
    lock_request.l_type = F_WRLCK;
    lock_request.l_whence = SEEK_SET;
    lock_request.l_start = 0;
    lock_request.l_len = 0;
    while (::fcntl(handle_, F_SETLKW, &lock_request) == -1) {
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
    release_lock(handle_);
}

FileLock::FileLock(FileLock&& other) noexcept : handle_(other.handle_) {
    other.handle_ = -1;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release_lock(handle_);
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
