#include "remediation/json_backup_store.h"

#include "core/json_persistence.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gno {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxTransactionBytes = 256 * 1024;
constexpr std::size_t kMaxResolvedTransactions = 100;
constexpr uint32_t kBackupVersion = 1;
constexpr char kProducer[] = "E2E4 Soft";

std::mutex& backupStoreMutex() {
    static std::mutex mutex;
    return mutex;
}

struct FileIdentity {
    uint64_t first = 0;
    uint64_t second = 0;
    uint64_t size = 0;
    int64_t modified_seconds = 0;
    int64_t modified_fraction = 0;
};

bool operator==(const FileIdentity& left, const FileIdentity& right) noexcept {
    return left.first == right.first && left.second == right.second &&
           left.size == right.size &&
           left.modified_seconds == right.modified_seconds &&
           left.modified_fraction == right.modified_fraction;
}

struct StoredFile {
    std::string name;
    std::string content;
    FileIdentity identity;
};

struct DirectoryEntry {
    std::string name;
    FileIdentity identity;
};

enum class ReadStatus { Success, Missing, Failure };

struct ReadFileResult {
    ReadStatus status = ReadStatus::Failure;
    StoredFile file;
};

bool safeLeafName(std::string_view name) noexcept {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos;
}

#ifndef _WIN32
class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() { reset(); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    int get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ >= 0; }
    int release() noexcept {
        const int result = value_;
        value_ = -1;
        return result;
    }
    void reset(int replacement = -1) noexcept {
        if (value_ >= 0) (void)::close(value_);
        value_ = replacement;
    }

private:
    int value_ = -1;
};

FileIdentity identityFromStat(const struct stat& info) noexcept {
    FileIdentity identity;
    identity.first = static_cast<uint64_t>(info.st_dev);
    identity.second = static_cast<uint64_t>(info.st_ino);
    identity.size = static_cast<uint64_t>(info.st_size);
#ifdef __APPLE__
    identity.modified_seconds = info.st_mtimespec.tv_sec;
    identity.modified_fraction = info.st_mtimespec.tv_nsec;
#else
    identity.modified_seconds = info.st_mtim.tv_sec;
    identity.modified_fraction = info.st_mtim.tv_nsec;
#endif
    return identity;
}
#else
class WindowsHandle {
public:
    WindowsHandle() = default;
    explicit WindowsHandle(HANDLE value) : value_(value) {}
    ~WindowsHandle() { reset(); }
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;
    WindowsHandle(WindowsHandle&& other) noexcept : value_(other.release()) {}
    WindowsHandle& operator=(WindowsHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept {
        return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
    }
    HANDLE release() noexcept {
        const HANDLE result = value_;
        value_ = INVALID_HANDLE_VALUE;
        return result;
    }
    void reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
        if (*this) CloseHandle(value_);
        value_ = replacement;
    }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

FileIdentity identityFromHandle(HANDLE handle, bool& regular) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    regular = GetFileInformationByHandle(handle, &info) != 0 &&
              (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
              (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    FileIdentity identity;
    if (!regular) return identity;
    identity.first = info.dwVolumeSerialNumber;
    identity.second = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) |
                      info.nFileIndexLow;
    identity.size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) |
                    info.nFileSizeLow;
    identity.modified_seconds =
        static_cast<int64_t>((static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime)
                              << 32) |
                             info.ftLastWriteTime.dwLowDateTime);
    return identity;
}

constexpr DWORD kDirectoryAccess =
    GENERIC_READ | GENERIC_WRITE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES;
constexpr DWORD kDirectoryShare = FILE_SHARE_READ | FILE_SHARE_WRITE;
constexpr DWORD kDirectoryFlags =
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;

bool isSafeDirectoryHandle(HANDLE handle) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(handle, &info) != 0 &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

WindowsHandle openWritableDirectory(const std::filesystem::path& path) {
    return WindowsHandle(CreateFileW(
        path.c_str(), kDirectoryAccess, kDirectoryShare, nullptr, OPEN_EXISTING,
        kDirectoryFlags, nullptr));
}
#endif

class TransactionDirectory {
public:
    using FailurePoint = JsonBackupStore::FailurePoint;
    using FailureInjector = JsonBackupStore::FailureInjector;

    TransactionDirectory(const TransactionDirectory&) = delete;
    TransactionDirectory& operator=(const TransactionDirectory&) = delete;
    TransactionDirectory(TransactionDirectory&&) noexcept = default;
    TransactionDirectory& operator=(TransactionDirectory&&) noexcept = default;

    static std::optional<TransactionDirectory> open(
        const std::filesystem::path& path, bool create,
        FailureInjector injector, bool& missing) {
        missing = false;
        try {
            const auto absolute = std::filesystem::absolute(path).lexically_normal();
#ifdef _WIN32
            TransactionDirectory result(absolute, std::move(injector));
            std::filesystem::path current = absolute.root_path();
            for (const auto& component : absolute.relative_path()) {
                if (component == L"." || component.empty()) continue;
                if (component == L"..") return std::nullopt;
                current /= component;
                bool created = false;
                HANDLE handle = CreateFileW(
                    current.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                    kDirectoryShare, nullptr, OPEN_EXISTING, kDirectoryFlags,
                    nullptr);
                if (handle == INVALID_HANDLE_VALUE &&
                    (GetLastError() == ERROR_FILE_NOT_FOUND ||
                     GetLastError() == ERROR_PATH_NOT_FOUND)) {
                    if (!create) {
                        missing = true;
                        return std::nullopt;
                    }
                    const bool directory_created =
                        CreateDirectoryW(current.c_str(), nullptr) != 0;
                    if (!directory_created &&
                        GetLastError() != ERROR_ALREADY_EXISTS) {
                        return std::nullopt;
                    }
                    created = directory_created;
                    handle = CreateFileW(current.c_str(),
                                         created ? kDirectoryAccess
                                                 : FILE_LIST_DIRECTORY |
                                                       FILE_READ_ATTRIBUTES,
                                         kDirectoryShare, nullptr, OPEN_EXISTING,
                                         kDirectoryFlags, nullptr);
                }
                WindowsHandle owned(handle);
                if (!owned || !isSafeDirectoryHandle(owned.get())) {
                    return std::nullopt;
                }
                if (created) {
                    if (result.inject(FailurePoint::SyncDirectory, current)) {
                        return std::nullopt;
                    }
                    auto parent = openWritableDirectory(current.parent_path());
                    if (!parent || !isSafeDirectoryHandle(parent.get()) ||
                        !FlushFileBuffers(owned.get()) ||
                        !FlushFileBuffers(parent.get())) {
                        return std::nullopt;
                    }
                }
                result.ancestor_handles_.push_back(std::move(owned));
            }
            if (result.ancestor_handles_.empty()) return std::nullopt;
            result.sync_handle_ = openWritableDirectory(absolute);
            if (!result.sync_handle_ ||
                !isSafeDirectoryHandle(result.sync_handle_.get())) {
                return std::nullopt;
            }
            return std::optional<TransactionDirectory>{std::move(result)};
#else
            FileDescriptor current(::open(absolute.is_absolute() ? "/" : ".",
                                          O_RDONLY | O_DIRECTORY | O_CLOEXEC));
            if (!current) return std::nullopt;
            std::filesystem::path current_path = absolute.root_path();
            for (const auto& component : absolute.relative_path()) {
                const auto name = component.string();
                if (name.empty() || name == ".") continue;
                if (name == ".." || !safeLeafName(name)) return std::nullopt;
                current_path /= component;
                int descriptor = ::openat(
                    current.get(), name.c_str(),
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                bool created = false;
                if (descriptor < 0 && errno == ENOENT) {
                    if (!create) {
                        missing = true;
                        return std::nullopt;
                    }
                    const int mkdir_result =
                        ::mkdirat(current.get(), name.c_str(), 0700);
                    if (mkdir_result != 0 && errno != EEXIST) {
                        return std::nullopt;
                    }
                    created = mkdir_result == 0;
                    descriptor = ::openat(
                        current.get(), name.c_str(),
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                }
                FileDescriptor next(descriptor);
                if (!next) return std::nullopt;
                struct stat info{};
                if (::fstat(next.get(), &info) != 0 || !S_ISDIR(info.st_mode)) {
                    return std::nullopt;
                }
                if (created) {
                    if ((injector && injector(FailurePoint::SyncDirectory, current_path)) ||
                        ::fsync(next.get()) != 0 || ::fsync(current.get()) != 0) {
                        return std::nullopt;
                    }
                }
                current = std::move(next);
            }
            return TransactionDirectory(
                absolute, std::move(injector), std::move(current));
#endif
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    bool lock() {
        const auto path = displayPath(".store.lock");
#ifdef _WIN32
        WindowsHandle handle(CreateFileW(
            path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle) return false;
        bool regular = false;
        (void)identityFromHandle(handle.get(), regular);
        if (!regular) return false;
        OVERLAPPED overlapped{};
        if (!LockFileEx(handle.get(), LOCKFILE_EXCLUSIVE_LOCK, 0,
                        MAXDWORD, MAXDWORD, &overlapped)) {
            return false;
        }
        lock_handle_ = std::move(handle);
        return true;
#else
        bool created = false;
        int descriptor = ::openat(directory_.get(), ".store.lock",
                                  O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC |
                                      O_NOFOLLOW,
                                  0600);
        if (descriptor >= 0) {
            created = true;
        } else if (errno == EEXIST) {
            descriptor = ::openat(directory_.get(), ".store.lock",
                                  O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        }
        FileDescriptor handle(descriptor);
        if (!handle) return false;
        struct stat info{};
        if (::fstat(handle.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_nlink != 1) {
            return false;
        }
        if (created &&
            (::fsync(handle.get()) != 0 || !syncDirectory())) {
            return false;
        }
        while (::flock(handle.get(), LOCK_EX) != 0) {
            if (errno != EINTR) return false;
        }
        lock_descriptor_ = std::move(handle);
        return true;
#endif
    }

    ReadFileResult read(std::string_view name, std::size_t maximum) const {
        ReadFileResult result;
        if (!safeLeafName(name)) return result;
        const auto path = displayPath(name);
#ifdef _WIN32
        WindowsHandle handle(CreateFileW(
            path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) result.status = ReadStatus::Missing;
            return result;
        }
        bool regular = false;
        const auto before = identityFromHandle(handle.get(), regular);
        if (!regular || before.size > maximum || before.size > MAXDWORD) return result;
        try {
            result.file.content.resize(static_cast<std::size_t>(before.size));
        } catch (const std::exception&) {
            return result;
        }
        DWORD offset = 0;
        while (offset < result.file.content.size()) {
            DWORD count = 0;
            if (!ReadFile(handle.get(), result.file.content.data() + offset,
                          static_cast<DWORD>(result.file.content.size() - offset),
                          &count, nullptr) || count == 0) {
                return result;
            }
            offset += count;
        }
        if (inject(FailurePoint::BeforeReadVerification, path)) return result;
        char extra = 0;
        DWORD extra_count = 0;
        if (!ReadFile(handle.get(), &extra, 1, &extra_count, nullptr) ||
            extra_count != 0) return result;
        bool still_regular = false;
        const auto after = identityFromHandle(handle.get(), still_regular);
        if (!still_regular || !(before == after)) return result;
#else
        FileDescriptor handle(::openat(
            directory_.get(), std::string(name).c_str(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (!handle) {
            if (errno == ENOENT) result.status = ReadStatus::Missing;
            return result;
        }
        struct stat before_info{};
        if (::fstat(handle.get(), &before_info) != 0 ||
            !S_ISREG(before_info.st_mode) || before_info.st_size < 0 ||
            static_cast<uint64_t>(before_info.st_size) > maximum) {
            return result;
        }
        const auto before = identityFromStat(before_info);
        try {
            result.file.content.resize(static_cast<std::size_t>(before.size));
        } catch (const std::exception&) {
            return result;
        }
        std::size_t offset = 0;
        while (offset < result.file.content.size()) {
            const auto count = ::read(handle.get(),
                                      result.file.content.data() + offset,
                                      result.file.content.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return result;
            offset += static_cast<std::size_t>(count);
        }
        if (inject(FailurePoint::BeforeReadVerification, path)) return result;
        char extra = 0;
        ssize_t extra_count = -1;
        do {
            extra_count = ::read(handle.get(), &extra, 1);
        } while (extra_count < 0 && errno == EINTR);
        if (extra_count != 0) return result;
        struct stat after_info{};
        if (::fstat(handle.get(), &after_info) != 0 ||
            !S_ISREG(after_info.st_mode) ||
            !(before == identityFromStat(after_info))) {
            return result;
        }
#endif
        result.status = ReadStatus::Success;
        result.file.name = std::string(name);
        result.file.identity = before;
        return result;
    }

    bool write(std::string_view name, const std::string& content,
               const std::optional<FileIdentity>& expected) {
        if (!safeLeafName(name)) return false;
        static std::atomic<unsigned long> sequence{0};
        const auto path = displayPath(name);
#ifdef _WIN32
        std::filesystem::path temporary;
        WindowsHandle handle;
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            temporary = path;
            temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) +
                         L"." + std::to_wstring(++sequence);
            handle.reset(CreateFileW(
                temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (handle) break;
        }
        if (!handle) return false;
        bool ok = !inject(FailurePoint::Write, temporary);
        std::size_t offset = 0;
        while (ok && offset < content.size()) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
                content.size() - offset, 1u << 20));
            ok = WriteFile(handle.get(), content.data() + offset, chunk,
                           &written, nullptr) != 0 && written != 0;
            offset += written;
        }
        if (ok) ok = !inject(FailurePoint::FlushFile, temporary) &&
                     FlushFileBuffers(handle.get()) != 0;
        handle.reset();
        if (ok) ok = targetMatches(name, expected);
        if (ok) {
            const DWORD flags = MOVEFILE_WRITE_THROUGH |
                                (expected ? MOVEFILE_REPLACE_EXISTING : 0);
            ok = !inject(FailurePoint::Replace, path) &&
                 MoveFileExW(temporary.c_str(), path.c_str(), flags) != 0;
        }
        if (!ok) DeleteFileW(temporary.c_str());
        return ok;
#else
        std::string temporary;
        FileDescriptor handle;
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            temporary = std::string(name) + ".tmp." +
                        std::to_string(::getpid()) + "." +
                        std::to_string(++sequence);
            handle.reset(::openat(
                directory_.get(), temporary.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
            if (handle) break;
        }
        if (!handle) return false;
        bool ok = !inject(FailurePoint::Write, displayPath(temporary));
        std::size_t offset = 0;
        while (ok && offset < content.size()) {
            const auto count = ::write(handle.get(), content.data() + offset,
                                       content.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            ok = count > 0;
            if (ok) offset += static_cast<std::size_t>(count);
        }
        if (ok) ok = !inject(FailurePoint::FlushFile, displayPath(temporary)) &&
                     ::fsync(handle.get()) == 0;
        const int raw = handle.release();
        if (raw >= 0) {
            int close_result = 0;
            do {
                close_result = ::close(raw);
            } while (close_result != 0 && errno == EINTR);
            ok = ok && close_result == 0;
        }
        if (ok) ok = targetMatches(name, expected);
        if (ok && !inject(FailurePoint::Replace, path)) {
            if (expected) {
                ok = ::renameat(directory_.get(), temporary.c_str(),
                                directory_.get(), std::string(name).c_str()) == 0;
            } else {
                ok = ::linkat(directory_.get(), temporary.c_str(),
                              directory_.get(), std::string(name).c_str(), 0) == 0;
                if (ok) {
                    ok = ::unlinkat(directory_.get(), temporary.c_str(), 0) == 0;
                    if (ok) temporary.clear();
                }
            }
        } else if (ok) {
            ok = false;
        }
        if (ok) ok = syncDirectory();
        if (!ok && !temporary.empty()) {
            ::unlinkat(directory_.get(), temporary.c_str(), 0);
        }
        return ok;
#endif
    }

    bool entries(std::vector<DirectoryEntry>& output) const {
#ifdef _WIN32
        const auto pattern = path_ / L"*";
        WIN32_FIND_DATAW data{};
        HANDLE search = FindFirstFileW(pattern.c_str(), &data);
        if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
        bool ok = true;
        do {
            const std::wstring wide_name(data.cFileName);
            if (wide_name == L"." || wide_name == L"..") continue;
            std::filesystem::path candidate = path_ / wide_name;
            WindowsHandle handle(CreateFileW(
                candidate.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!handle) { ok = false; break; }
            bool regular = false;
            const auto identity = identityFromHandle(handle.get(), regular);
            if (!regular) continue;
            const auto name = std::filesystem::path(wide_name).u8string();
            output.push_back({name, identity});
        } while (FindNextFileW(search, &data));
        if (GetLastError() != ERROR_NO_MORE_FILES) ok = false;
        FindClose(search);
        return ok;
#else
        const int duplicate = ::openat(
            directory_.get(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (duplicate < 0) return false;
        DIR* stream = ::fdopendir(duplicate);
        if (!stream) {
            ::close(duplicate);
            return false;
        }
        bool ok = true;
        errno = 0;
        while (const auto* item = ::readdir(stream)) {
            const std::string name(item->d_name);
            if (name == "." || name == "..") continue;
            struct stat info{};
            if (::fstatat(directory_.get(), name.c_str(), &info,
                          AT_SYMLINK_NOFOLLOW) != 0) {
                ok = false;
                break;
            }
            if (!S_ISREG(info.st_mode)) continue;
            output.push_back({name, identityFromStat(info)});
            errno = 0;
        }
        if (errno != 0) ok = false;
        if (::closedir(stream) != 0) ok = false;
        return ok;
#endif
    }

    bool remove(std::string_view name, const FileIdentity& expected) {
        if (!safeLeafName(name)) return false;
        const auto path = displayPath(name);
        if (inject(FailurePoint::Remove, path) ||
            !targetMatches(name, std::optional<FileIdentity>{expected})) {
            return false;
        }
#ifdef _WIN32
        if (!DeleteFileW(path.c_str())) return false;
        return syncDirectory();
#else
        if (::unlinkat(directory_.get(), std::string(name).c_str(), 0) != 0) {
            return false;
        }
        return syncDirectory();
#endif
    }

    std::filesystem::path displayPath(std::string_view leaf) const {
        return path_ / std::filesystem::path(std::string(leaf));
    }

private:
#ifdef _WIN32
    TransactionDirectory(std::filesystem::path path, FailureInjector injector)
        : path_(std::move(path)), injector_(std::move(injector)) {}
#else
    TransactionDirectory(std::filesystem::path path, FailureInjector injector,
                         FileDescriptor directory)
        : path_(std::move(path)), injector_(std::move(injector)),
          directory_(std::move(directory)) {}
#endif

    bool inject(FailurePoint point, const std::filesystem::path& path) const {
        try {
            return injector_ && injector_(point, path);
        } catch (...) {
            return true;
        }
    }

    bool targetMatches(
        std::string_view name,
        const std::optional<FileIdentity>& expected) const {
#ifdef _WIN32
        const auto path = displayPath(name);
        WindowsHandle handle(CreateFileW(
            path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle) {
            return !expected && GetLastError() == ERROR_FILE_NOT_FOUND;
        }
        bool regular = false;
        const auto identity = identityFromHandle(handle.get(), regular);
        return expected && regular && identity == *expected;
#else
        struct stat info{};
        if (::fstatat(directory_.get(), std::string(name).c_str(), &info,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            return !expected && errno == ENOENT;
        }
        return expected && S_ISREG(info.st_mode) &&
               identityFromStat(info) == *expected;
#endif
    }

    bool syncDirectory() const {
        if (inject(FailurePoint::SyncDirectory, path_)) return false;
#ifdef _WIN32
        return sync_handle_ && FlushFileBuffers(sync_handle_.get()) != 0;
#else
        return ::fsync(directory_.get()) == 0;
#endif
    }

    std::filesystem::path path_;
    FailureInjector injector_;
#ifdef _WIN32
    std::vector<WindowsHandle> ancestor_handles_;
    WindowsHandle sync_handle_;
    WindowsHandle lock_handle_;
#else
    FileDescriptor directory_;
    FileDescriptor lock_descriptor_;
#endif
};

bool isValidUtf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index++]);
        if (lead == 0) return false;
        if (lead <= 0x7f) continue;

        std::size_t continuation_count = 0;
        uint32_t code_point = 0;
        uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            continuation_count = 1;
            code_point = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            continuation_count = 2;
            code_point = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            continuation_count = 3;
            code_point = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation_count > text.size() - index) return false;
        for (std::size_t offset = 0; offset < continuation_count; ++offset) {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xc0) != 0x80) return false;
            code_point = (code_point << 6) | (next & 0x3f);
        }
        if (code_point < minimum || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

template <typename T>
Result<T> backupFailure(std::string detail) {
    Result<T> result;
    result.error = RemediationError::BackupFailed;
    result.detail = std::move(detail);
    return result;
}

Result<std::monostate> backupSuccess() {
    Result<std::monostate> result;
    result.error = RemediationError::None;
    return result;
}

bool isTransactionId(std::string_view id) noexcept {
    if (id.size() != 36) return false;
    constexpr std::array<std::size_t, 4> separators{8, 13, 18, 23};
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (std::find(separators.begin(), separators.end(), index) != separators.end()) {
            if (id[index] != '-') return false;
            continue;
        }
        const bool digit = id[index] >= '0' && id[index] <= '9';
        const bool lower = id[index] >= 'a' && id[index] <= 'f';
        if (!digit && !lower) return false;
    }
    return id[14] == '4' && (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');
}

std::filesystem::path transactionsRoot(const std::filesystem::path& storage_root) {
    const auto root = storage_root.empty() ? persistence::applicationDataRoot() : storage_root;
    return root / "GNO" / "remediation" / "transactions";
}

bool isString(const json& value, std::size_t max_size, std::string& output) {
    if (!value.is_string()) return false;
    const auto& input = value.get_ref<const std::string&>();
    if (input.size() > max_size || !isValidUtf8(input)) return false;
    output = input;
    return true;
}

bool isUnsigned(const json& value, uint64_t maximum, uint64_t& output) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > maximum) return false;
        output = number;
        return true;
    }
    if (!value.is_number_integer()) return false;
    const auto number = value.get<int64_t>();
    if (number < 0 || static_cast<uint64_t>(number) > maximum) return false;
    output = static_cast<uint64_t>(number);
    return true;
}

bool isInt(const json& value, int64_t minimum, int64_t maximum, int64_t& output) {
    if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > static_cast<uint64_t>(maximum)) return false;
        output = static_cast<int64_t>(number);
        return true;
    }
    const auto number = value.get<int64_t>();
    if (number < minimum || number > maximum) return false;
    output = number;
    return true;
}

template <typename Enum>
bool readEnum(const json& value, Enum& output, std::size_t count) {
    uint64_t number = 0;
    if (!isUnsigned(value, count - 1, number)) return false;
    output = static_cast<Enum>(number);
    return true;
}

json writeTarget(const ActionTarget& target) {
    return std::visit(
        [](const auto& value) -> json {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, std::monostate>::value) {
                return {{"kind", "none"}};
            } else if constexpr (std::is_same<Value, InterfaceId>::value) {
                return {{"kind", "interface"}, {"value", value.value},
                        {"luid", value.luid}};
            } else if constexpr (std::is_same<Value, ExecutableIdentity>::value) {
                return {{"kind", "executable"}, {"path", value.canonical_path}};
            } else {
                return {{"kind", "process"},
                        {"pid", value.pid},
                        {"creation_time", value.creation_time},
                        {"path", value.executable_path}};
            }
        },
        target);
}

std::optional<ActionTarget> readTarget(const json& value) {
    try {
        if (!value.is_object()) return std::nullopt;
        std::string kind;
        if (!isString(value.at("kind"), 16, kind)) return std::nullopt;
        if (kind == "none") return ActionTarget{std::monostate{}};
        if (kind == "interface") {
            std::string interface_id;
            uint64_t luid = 0;
            if (!isString(value.at("value"), kMaxInterfaceIdLength, interface_id) ||
                interface_id.empty()) return std::nullopt;
            if (value.contains("luid") &&
                !isUnsigned(value.at("luid"), std::numeric_limits<uint64_t>::max(), luid)) {
                return std::nullopt;
            }
            return ActionTarget{InterfaceId{std::move(interface_id), luid}};
        }
        if (kind == "executable") {
            std::string path;
            if (!isString(value.at("path"), kMaxExecutablePathLength, path) || path.empty()) {
                return std::nullopt;
            }
            return ActionTarget{ExecutableIdentity{std::move(path)}};
        }
        if (kind == "process") {
            uint64_t pid = 0;
            uint64_t creation_time = 0;
            std::string path;
            if (!isUnsigned(value.at("pid"), std::numeric_limits<uint32_t>::max(), pid) ||
                pid == 0 ||
                !isUnsigned(value.at("creation_time"), std::numeric_limits<uint64_t>::max(),
                            creation_time) ||
                creation_time == 0 ||
                !isString(value.at("path"), kMaxExecutablePathLength, path) || path.empty()) {
                return std::nullopt;
            }
            return ActionTarget{ProcessIdentity{static_cast<uint32_t>(pid), creation_time,
                                                std::move(path)}};
        }
    } catch (const json::exception&) {
    }
    return std::nullopt;
}

json writeValue(const ActionValue& action_value) {
    return std::visit(
        [](const auto& value) -> json {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, std::monostate>::value) {
                return {{"kind", "none"}};
            } else if constexpr (std::is_same<Value, DnsValue>::value) {
                json servers = json::array();
                for (const auto& server : value.servers) servers.push_back(server.toString());
                return {{"kind", "dns"}, {"automatic", value.automatic}, {"servers", servers}};
            } else if constexpr (std::is_same<Value, MtuValue>::value) {
                return {{"kind", "mtu"}, {"bytes", value.bytes}};
            } else if constexpr (std::is_same<Value, TcpValue>::value) {
                json settings = json::array();
                for (const auto& setting : value.settings) {
                    settings.push_back({{"parameter", static_cast<unsigned>(setting.parameter)},
                                        {"existed", setting.existed}, {"value", setting.value}});
                }
                return {{"kind", "tcp"}, {"settings", settings}};
            } else if constexpr (std::is_same<Value, PowerPlanValue>::value) {
                return {{"kind", "power_plan"}, {"identifier", value.identifier}};
            } else if constexpr (std::is_same<Value, EnergyValue>::value) {
                return {{"kind", "energy"}, {"mode", static_cast<unsigned>(value.mode)}};
            } else if constexpr (std::is_same<Value, RegistryValue>::value) {
                json result = {{"kind", "registry"}, {"existed", value.existed},
                               {"key_existed", value.key_existed}};
                std::visit(
                    [&result](const auto& scalar) {
                        using Scalar = std::decay_t<decltype(scalar)>;
                        if constexpr (std::is_same<Scalar, std::monostate>::value) {
                            result["value_kind"] = "none";
                        } else if constexpr (std::is_same<Scalar, uint32_t>::value) {
                            result["value_kind"] = "uint32";
                            result["value"] = scalar;
                        } else if constexpr (std::is_same<Scalar, int64_t>::value) {
                            result["value_kind"] = "int64";
                            result["value"] = scalar;
                        } else {
                            result["value_kind"] = "string";
                            result["value"] = scalar;
                        }
                    },
                    value.value);
                return result;
            } else if constexpr (std::is_same<Value, GameDvrValue>::value) {
                const auto write_registry = [](const RegistryValue& registry) {
                    json result = {{"existed", registry.existed},
                                   {"key_existed", registry.key_existed}};
                    std::visit(
                        [&result](const auto& scalar) {
                            using Scalar = std::decay_t<decltype(scalar)>;
                            if constexpr (std::is_same<Scalar, std::monostate>::value) {
                                result["value_kind"] = "none";
                            } else if constexpr (std::is_same<Scalar, uint32_t>::value) {
                                result["value_kind"] = "uint32";
                                result["value"] = scalar;
                            } else if constexpr (std::is_same<Scalar, int64_t>::value) {
                                result["value_kind"] = "int64";
                                result["value"] = scalar;
                            } else {
                                result["value_kind"] = "string";
                                result["value"] = scalar;
                            }
                        },
                        registry.value);
                    return result;
                };
                return {{"kind", "game_dvr"},
                        {"game_dvr_enabled", write_registry(value.game_dvr_enabled)},
                        {"app_capture_enabled", write_registry(value.app_capture_enabled)}};
            } else if constexpr (std::is_same<Value, FullscreenValue>::value) {
                return {{"kind", "fullscreen"}, {"existed", value.existed},
                        {"flags", value.compatibility_flags},
                        {"key_existed", value.key_existed}};
            } else if constexpr (std::is_same<Value, PriorityValue>::value) {
                return {{"kind", "priority"}, {"value", static_cast<unsigned>(value)}};
            } else {
                return {{"kind", "nice"}, {"value", value.value}};
            }
        },
        action_value);
}

std::optional<ActionValue> readValue(const json& value) {
    try {
        if (!value.is_object()) return std::nullopt;
        std::string kind;
        if (!isString(value.at("kind"), 32, kind)) return std::nullopt;
        if (kind == "none") return ActionValue{std::monostate{}};
        if (kind == "dns") {
            if (!value.at("automatic").is_boolean() || !value.at("servers").is_array() ||
                value.at("servers").size() > kMaxDnsServers) return std::nullopt;
            DnsValue dns;
            dns.automatic = value.at("automatic").get<bool>();
            for (const auto& server : value.at("servers")) {
                std::string text;
                if (!isString(server, 15, text)) return std::nullopt;
                const auto parsed = Ipv4Address::parse(text);
                if (!parsed) return std::nullopt;
                dns.servers.push_back(*parsed);
            }
            return ActionValue{std::move(dns)};
        }
        if (kind == "mtu") {
            uint64_t bytes = 0;
            if (!isUnsigned(value.at("bytes"), std::numeric_limits<uint32_t>::max(), bytes)) {
                return std::nullopt;
            }
            return ActionValue{MtuValue{static_cast<uint32_t>(bytes)}};
        }
        if (kind == "tcp") {
            const auto& settings = value.at("settings");
            if (!settings.is_array() || settings.size() > kMaxTcpSettings) return std::nullopt;
            TcpValue tcp;
            for (const auto& item : settings) {
                if (!item.is_object() || !item.at("existed").is_boolean()) return std::nullopt;
                TcpParameter parameter;
                int64_t setting_value = 0;
                if (!readEnum(item.at("parameter"), parameter, 3) ||
                    !isInt(item.at("value"), std::numeric_limits<int32_t>::min(),
                           std::numeric_limits<int32_t>::max(), setting_value)) return std::nullopt;
                tcp.settings.push_back(TcpSetting{parameter, item.at("existed").get<bool>(),
                                                  static_cast<int32_t>(setting_value)});
            }
            return ActionValue{std::move(tcp)};
        }
        if (kind == "power_plan") {
            std::string identifier;
            if (!isString(value.at("identifier"), kMaxPowerPlanIdLength, identifier) ||
                identifier.empty()) return std::nullopt;
            return ActionValue{PowerPlanValue{std::move(identifier)}};
        }
        if (kind == "energy") {
            EnergyMode mode;
            if (!readEnum(value.at("mode"), mode, 4)) return std::nullopt;
            return ActionValue{EnergyValue{mode}};
        }
        if (kind == "registry") {
            if (!value.at("existed").is_boolean()) return std::nullopt;
            std::string value_kind;
            if (!isString(value.at("value_kind"), 16, value_kind)) return std::nullopt;
            RegistryScalar scalar;
            if (value_kind == "none") {
                scalar = std::monostate{};
            } else if (value_kind == "uint32") {
                uint64_t number = 0;
                if (!isUnsigned(value.at("value"), std::numeric_limits<uint32_t>::max(), number)) {
                    return std::nullopt;
                }
                scalar = static_cast<uint32_t>(number);
            } else if (value_kind == "int64") {
                int64_t number = 0;
                if (!isInt(value.at("value"), std::numeric_limits<int64_t>::min(),
                           std::numeric_limits<int64_t>::max(), number)) return std::nullopt;
                scalar = number;
            } else if (value_kind == "string") {
                std::string text;
                if (!isString(value.at("value"), kMaxRegistryStringLength, text)) return std::nullopt;
                scalar = std::move(text);
            } else {
                return std::nullopt;
            }
            const bool key_existed = !value.contains("key_existed") ||
                                     (value.at("key_existed").is_boolean() &&
                                      value.at("key_existed").get<bool>());
            if (value.contains("key_existed") &&
                !value.at("key_existed").is_boolean()) return std::nullopt;
            return ActionValue{RegistryValue{value.at("existed").get<bool>(),
                                             std::move(scalar), key_existed}};
        }
        if (kind == "game_dvr") {
            const auto read_registry = [](const json& item) -> std::optional<RegistryValue> {
                if (!item.is_object() || !item.at("existed").is_boolean()) {
                    return std::nullopt;
                }
                std::string value_kind;
                if (!isString(item.at("value_kind"), 16, value_kind)) {
                    return std::nullopt;
                }
                RegistryScalar scalar;
                if (value_kind == "none") {
                    scalar = std::monostate{};
                } else if (value_kind == "uint32") {
                    uint64_t number = 0;
                    if (!isUnsigned(item.at("value"),
                                    std::numeric_limits<uint32_t>::max(), number)) {
                        return std::nullopt;
                    }
                    scalar = static_cast<uint32_t>(number);
                } else if (value_kind == "int64") {
                    int64_t number = 0;
                    if (!isInt(item.at("value"),
                               std::numeric_limits<int64_t>::min(),
                               std::numeric_limits<int64_t>::max(), number)) {
                        return std::nullopt;
                    }
                    scalar = number;
                } else if (value_kind == "string") {
                    std::string text;
                    if (!isString(item.at("value"), kMaxRegistryStringLength, text)) {
                        return std::nullopt;
                    }
                    scalar = std::move(text);
                } else {
                    return std::nullopt;
                }
                const bool key_existed = !item.contains("key_existed") ||
                                         (item.at("key_existed").is_boolean() &&
                                          item.at("key_existed").get<bool>());
                if (item.contains("key_existed") &&
                    !item.at("key_existed").is_boolean()) return std::nullopt;
                return RegistryValue{item.at("existed").get<bool>(),
                                     std::move(scalar), key_existed};
            };
            const auto game_dvr = read_registry(value.at("game_dvr_enabled"));
            const auto app_capture = read_registry(value.at("app_capture_enabled"));
            if (!game_dvr || !app_capture) return std::nullopt;
            return ActionValue{GameDvrValue{*game_dvr, *app_capture}};
        }
        if (kind == "fullscreen") {
            if (!value.at("existed").is_boolean()) return std::nullopt;
            std::string flags;
            if (!isString(value.at("flags"), kMaxRegistryStringLength, flags)) return std::nullopt;
            const bool key_existed = !value.contains("key_existed") ||
                                     (value.at("key_existed").is_boolean() &&
                                      value.at("key_existed").get<bool>());
            if (value.contains("key_existed") &&
                !value.at("key_existed").is_boolean()) return std::nullopt;
            return ActionValue{FullscreenValue{value.at("existed").get<bool>(),
                                               std::move(flags), key_existed}};
        }
        if (kind == "priority") {
            PriorityValue priority;
            if (!readEnum(value.at("value"), priority, 6)) return std::nullopt;
            return ActionValue{priority};
        }
        if (kind == "nice") {
            int64_t nice = 0;
            if (!isInt(value.at("value"), kMinNiceValue, kMaxNiceValue, nice)) return std::nullopt;
            return ActionValue{NiceValue{static_cast<int>(nice)}};
        }
    } catch (const json::exception&) {
    }
    return std::nullopt;
}

json writeState(const ActionState& state) {
    return {{"id", static_cast<unsigned>(state.id)},
            {"status", static_cast<unsigned>(state.status)},
            {"value", writeValue(state.value)},
            {"detail", state.detail}};
}

std::optional<ActionState> readState(const json& value) {
    try {
        if (!value.is_object()) return std::nullopt;
        ActionId id;
        ActionStatus status;
        std::string detail;
        const auto action_value = readValue(value.at("value"));
        if (!readEnum(value.at("id"), id, 8) || !readEnum(value.at("status"), status, 7) ||
            !action_value || !isString(value.at("detail"), kMaxDetailLength, detail)) {
            return std::nullopt;
        }
        return ActionState{id, status, *action_value, std::move(detail)};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

json writePreparedAction(const PreparedAction& action) {
    return {{"id", static_cast<unsigned>(action.id)},
            {"target", writeTarget(action.target)},
            {"before", writeState(action.before)},
            {"proposed", writeState(action.proposed)},
            {"rollback_supported", action.rollback_supported}};
}

std::optional<PreparedAction> readPreparedAction(const json& value) {
    try {
        if (!value.is_object() || !value.at("rollback_supported").is_boolean()) return std::nullopt;
        ActionId id;
        const auto target = readTarget(value.at("target"));
        const auto before = readState(value.at("before"));
        const auto proposed = readState(value.at("proposed"));
        if (!readEnum(value.at("id"), id, 8) || !target || !before || !proposed ||
            before->id != id || proposed->id != id || !isBounded(*target) ||
            !isBounded(before->value) || !isBounded(proposed->value) ||
            before->detail.size() > kMaxDetailLength || proposed->detail.size() > kMaxDetailLength) {
            return std::nullopt;
        }
        return PreparedAction{id, *target, *before, *proposed,
                              value.at("rollback_supported").get<bool>()};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

json writeOutcome(const ActionOutcome& outcome) {
    return {{"id", static_cast<unsigned>(outcome.id)},
            {"status", static_cast<unsigned>(outcome.status)},
            {"error", static_cast<unsigned>(outcome.error)},
            {"attempted", outcome.attempted},
            {"state", writeState(outcome.state)},
            {"detail", outcome.detail},
            {"rollback_error", static_cast<unsigned>(outcome.rollback_error)},
            {"rollback_attempted", outcome.rollback_attempted},
            {"rollback_detail", outcome.rollback_detail}};
}

std::optional<ActionOutcome> readOutcome(const json& value) {
    try {
        if (!value.is_object() || !value.at("attempted").is_boolean() ||
            !value.at("rollback_attempted").is_boolean()) return std::nullopt;
        ActionId id;
        ActionStatus status;
        RemediationError error;
        RemediationError rollback_error;
        std::string detail;
        std::string rollback_detail;
        const auto state = readState(value.at("state"));
        if (!readEnum(value.at("id"), id, 8) || !readEnum(value.at("status"), status, 7) ||
            !readEnum(value.at("error"), error, 14) ||
            !readEnum(value.at("rollback_error"), rollback_error, 14) || !state ||
            state->id != id || !isBounded(state->value) ||
            !isString(value.at("detail"), kMaxDetailLength, detail) ||
            !isString(value.at("rollback_detail"), kMaxDetailLength, rollback_detail)) {
            return std::nullopt;
        }
        return ActionOutcome{id, status, error, value.at("attempted").get<bool>(), *state,
                             std::move(detail), rollback_error,
                             value.at("rollback_attempted").get<bool>(),
                             std::move(rollback_detail)};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

json writeRecord(const TransactionRecord& record) {
    json prepared = json::array();
    for (const auto& action : record.prepared_actions) prepared.push_back(writePreparedAction(action));
    json outcomes = json::array();
    for (const auto& outcome : record.outcomes) outcomes.push_back(writeOutcome(outcome));
    json action_order = json::array();
    for (const auto id : record.action_order) action_order.push_back(static_cast<unsigned>(id));
    json applied_order = json::array();
    for (const auto id : record.applied_action_order) applied_order.push_back(static_cast<unsigned>(id));
    return {{"schema_version", record.schema_version},
            {"transaction_id", record.transaction_id},
            {"status", static_cast<unsigned>(record.status)},
            {"prepared_actions", prepared},
            {"outcomes", outcomes},
            {"action_order", action_order},
            {"applied_action_order", applied_order},
            {"error", static_cast<unsigned>(record.error)},
            {"detail", record.detail}};
}

std::optional<std::vector<ActionId>> readActionOrder(const json& value, std::size_t maximum) {
    try {
        if (!value.is_array() || value.size() > maximum) return std::nullopt;
        std::vector<ActionId> result;
        result.reserve(value.size());
        for (const auto& item : value) {
            ActionId id;
            if (!readEnum(item, id, 8)) return std::nullopt;
            result.push_back(id);
        }
        return result;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

bool compatibleActionValue(
    ActionId id, const ActionTarget& target, const ActionValue& value) noexcept {
    const bool no_target = std::holds_alternative<std::monostate>(target);
    switch (id) {
    case ActionId::PowerPlan:
        return no_target && std::holds_alternative<PowerPlanValue>(value);
    case ActionId::EnergyMode:
        return no_target && std::holds_alternative<EnergyValue>(value);
    case ActionId::GameDvr:
        return no_target &&
               (std::holds_alternative<RegistryValue>(value) ||
                std::holds_alternative<GameDvrValue>(value));
    case ActionId::FullscreenOptimizations:
        return std::holds_alternative<ExecutableIdentity>(target) &&
               std::holds_alternative<FullscreenValue>(value);
    case ActionId::TcpParameters:
        return no_target && std::holds_alternative<TcpValue>(value);
    case ActionId::Dns:
        return std::holds_alternative<InterfaceId>(target) &&
               std::holds_alternative<DnsValue>(value);
    case ActionId::Mtu:
        return std::holds_alternative<InterfaceId>(target) &&
               std::holds_alternative<MtuValue>(value);
    case ActionId::ProcessPriority:
        return std::holds_alternative<ProcessIdentity>(target) &&
               (std::holds_alternative<PriorityValue>(value) ||
                std::holds_alternative<NiceValue>(value));
    }
    return false;
}

bool validTarget(const ActionTarget& target) noexcept {
    if (!isBounded(target)) return false;
    return std::visit(
        [](const auto& value) noexcept {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, InterfaceId>::value) {
                return isValidUtf8(value.value);
            } else if constexpr (std::is_same<Value, ExecutableIdentity>::value) {
                return isValidUtf8(value.canonical_path);
            } else if constexpr (std::is_same<Value, ProcessIdentity>::value) {
                return isValidUtf8(value.executable_path);
            }
            return true;
        },
        target);
}

bool validActionValue(const ActionValue& value) noexcept {
    if (!isBounded(value)) return false;
    return std::visit(
        [](const auto& nested) noexcept {
            using Value = std::decay_t<decltype(nested)>;
            if constexpr (std::is_same<Value, TcpValue>::value) {
                return std::all_of(
                    nested.settings.begin(), nested.settings.end(),
                    [](const TcpSetting& setting) {
                        return static_cast<std::size_t>(setting.parameter) < 3;
                    });
            } else if constexpr (std::is_same<Value, PowerPlanValue>::value) {
                return isValidUtf8(nested.identifier);
            } else if constexpr (std::is_same<Value, EnergyValue>::value) {
                return static_cast<std::size_t>(nested.mode) < 4;
            } else if constexpr (std::is_same<Value, RegistryValue>::value) {
                if (!nested.existed &&
                    !std::holds_alternative<std::monostate>(nested.value)) {
                    return false;
                }
                const auto* text = std::get_if<std::string>(&nested.value);
                return !text || isValidUtf8(*text);
            } else if constexpr (std::is_same<Value, GameDvrValue>::value) {
                const auto valid_registry = [](const RegistryValue& registry) {
                    if (!registry.existed &&
                        !std::holds_alternative<std::monostate>(registry.value)) {
                        return false;
                    }
                    const auto* text = std::get_if<std::string>(&registry.value);
                    return !text || isValidUtf8(*text);
                };
                return valid_registry(nested.game_dvr_enabled) &&
                       valid_registry(nested.app_capture_enabled);
            } else if constexpr (std::is_same<Value, FullscreenValue>::value) {
                return isValidUtf8(nested.compatibility_flags);
            } else if constexpr (std::is_same<Value, PriorityValue>::value) {
                return static_cast<std::size_t>(nested) < 6;
            } else if constexpr (std::is_same<Value, NiceValue>::value) {
                return nested.value >= kMinNiceValue &&
                       nested.value <= kMaxNiceValue;
            }
            return true;
        },
        value);
}

bool hasCleanErrorDetail(RemediationError error, const std::string& detail) noexcept {
    return error != RemediationError::None || detail.empty();
}

bool validOutcomeBookkeeping(
    const ActionOutcome& outcome, const PreparedAction& action) noexcept {
    if (!hasCleanErrorDetail(outcome.error, outcome.detail) ||
        !hasCleanErrorDetail(outcome.rollback_error, outcome.rollback_detail) ||
        (!outcome.rollback_attempted &&
         (outcome.rollback_error != RemediationError::None ||
          !outcome.rollback_detail.empty()))) {
        return false;
    }

    const bool compatible_state =
        compatibleActionValue(action.id, action.target, outcome.state.value);
    switch (outcome.status) {
    case ActionStatus::NotChecked:
        return !outcome.attempted && outcome.error == RemediationError::None &&
               outcome.detail.empty() && !outcome.rollback_attempted &&
               outcome.state.status == ActionStatus::NotChecked &&
               std::holds_alternative<std::monostate>(outcome.state.value) &&
               outcome.state.detail.empty();
    case ActionStatus::Applied:
        return outcome.attempted && outcome.error == RemediationError::None &&
               outcome.detail.empty() && compatible_state &&
               (!outcome.rollback_attempted ||
                outcome.rollback_error != RemediationError::None);
    case ActionStatus::Failed:
        return outcome.attempted && outcome.error != RemediationError::None &&
               !outcome.rollback_attempted &&
               ((std::holds_alternative<std::monostate>(outcome.state.value) &&
                 outcome.state.status == ActionStatus::NotChecked &&
                 outcome.state.detail.empty()) ||
                compatible_state);
    case ActionStatus::Reverted:
        return outcome.attempted && outcome.error == RemediationError::None &&
               outcome.detail.empty() && compatible_state &&
               outcome.rollback_attempted &&
               outcome.rollback_error == RemediationError::None &&
               outcome.rollback_detail.empty();
    case ActionStatus::Recommended:
    case ActionStatus::AlreadyConfigured:
    case ActionStatus::Unsupported:
        return false;
    }
    return false;
}

bool validRecordShape(const TransactionRecord& record) {
    if (record.schema_version != kBackupVersion || !isTransactionId(record.transaction_id) ||
        static_cast<std::size_t>(record.status) >= 9 ||
        static_cast<std::size_t>(record.error) >= 14 ||
        record.prepared_actions.empty() ||
        record.prepared_actions.size() > kMaxTransactionActions ||
        record.outcomes.size() != record.prepared_actions.size() ||
        record.action_order.size() > kMaxTransactionActions ||
        record.applied_action_order.size() > record.prepared_actions.size() ||
        record.detail.size() > kMaxDetailLength ||
        !hasCleanErrorDetail(record.error, record.detail)) return false;

    std::array<bool, kMaxTransactionActions> seen{};
    for (std::size_t index = 0; index < record.prepared_actions.size(); ++index) {
        const auto& action = record.prepared_actions[index];
        const auto id = static_cast<std::size_t>(action.id);
        if (id >= seen.size() || seen[id] || action.before.id != action.id ||
            action.proposed.id != action.id || !validTarget(action.target) ||
            static_cast<std::size_t>(action.before.status) >= 7 ||
            static_cast<std::size_t>(action.proposed.status) >= 7 ||
            !compatibleActionValue(action.id, action.target, action.before.value) ||
            !compatibleActionValue(action.id, action.target, action.proposed.value) ||
            !validActionValue(action.before.value) ||
            !validActionValue(action.proposed.value) ||
            !isSafeProposed(action.proposed.value) ||
            action.before.detail.size() > kMaxDetailLength ||
            action.proposed.detail.size() > kMaxDetailLength ||
            !isValidUtf8(action.before.detail) ||
            !isValidUtf8(action.proposed.detail)) return false;
        seen[id] = true;
        const auto& outcome = record.outcomes[index];
        if (outcome.id != action.id || outcome.state.id != action.id ||
            static_cast<std::size_t>(outcome.status) >= 7 ||
            static_cast<std::size_t>(outcome.error) >= 14 ||
            static_cast<std::size_t>(outcome.rollback_error) >= 14 ||
            static_cast<std::size_t>(outcome.state.status) >= 7 ||
            !validActionValue(outcome.state.value) ||
            outcome.state.detail.size() > kMaxDetailLength ||
            outcome.detail.size() > kMaxDetailLength ||
            outcome.rollback_detail.size() > kMaxDetailLength ||
            !isValidUtf8(outcome.state.detail) ||
            !isValidUtf8(outcome.detail) ||
            !isValidUtf8(outcome.rollback_detail) ||
            !validOutcomeBookkeeping(outcome, action)) return false;
    }

    std::array<bool, kMaxTransactionActions> ordered{};
    for (const auto id : record.action_order) {
        const auto index = static_cast<std::size_t>(id);
        if (index >= seen.size() || !seen[index] || ordered[index]) return false;
        ordered[index] = true;
    }
    for (std::size_t index = 0; index < record.applied_action_order.size(); ++index) {
        if (record.applied_action_order[index] != record.prepared_actions[index].id) return false;
    }

    const auto applied_count = record.applied_action_order.size();
    for (std::size_t index = 0; index < record.outcomes.size(); ++index) {
        const auto status = record.outcomes[index].status;
        if (index < applied_count) {
            if (status != ActionStatus::Applied && status != ActionStatus::Reverted) {
                return false;
            }
        } else if (status == ActionStatus::Applied || status == ActionStatus::Reverted) {
            return false;
        }
    }

    std::size_t unresolved_count = 0;
    while (unresolved_count < applied_count &&
           record.outcomes[unresolved_count].status == ActionStatus::Applied) {
        ++unresolved_count;
    }
    for (std::size_t index = unresolved_count; index < applied_count; ++index) {
        if (record.outcomes[index].status != ActionStatus::Reverted) return false;
    }

    const auto valid_suffix = [&]() {
        std::size_t index = applied_count;
        if (index < record.outcomes.size() &&
            record.outcomes[index].status == ActionStatus::Failed) {
            ++index;
        }
        for (; index < record.outcomes.size(); ++index) {
            if (record.outcomes[index].status != ActionStatus::NotChecked) return false;
        }
        return true;
    };
    const auto apply_order_is_exact = [&]() {
        return record.action_order == record.applied_action_order;
    };
    const auto rollback_order_is_exact = [&](bool allow_empty) {
        if (record.action_order.empty()) return allow_empty;
        const auto reverted_count = applied_count - unresolved_count;
        if (record.action_order.size() > reverted_count) return false;
        for (std::size_t index = 0; index < record.action_order.size(); ++index) {
            const auto prepared_index =
                unresolved_count + record.action_order.size() - index - 1;
            if (record.action_order[index] !=
                record.prepared_actions[prepared_index].id) return false;
        }
        return true;
    };
    const auto all_not_checked_after_prefix = [&]() {
        for (std::size_t index = applied_count; index < record.outcomes.size(); ++index) {
            if (record.outcomes[index].status != ActionStatus::NotChecked) return false;
        }
        return true;
    };

    const auto rollback_failure_metadata_is_exact = [&]() {
        std::size_t failure_count = 0;
        std::size_t failure_index = 0;
        for (std::size_t index = 0; index < applied_count; ++index) {
            const auto& outcome = record.outcomes[index];
            if (outcome.status == ActionStatus::Applied &&
                outcome.rollback_attempted) {
                if (outcome.rollback_error == RemediationError::None ||
                    ++failure_count > 1) {
                    return false;
                }
                failure_index = index;
            }
        }
        if (failure_count == 0) return true;
        return unresolved_count > 0 && failure_index == unresolved_count - 1;
    };
    const auto no_rollback_failure_metadata = [&]() {
        for (std::size_t index = 0; index < applied_count; ++index) {
            const auto& outcome = record.outcomes[index];
            if (outcome.status == ActionStatus::Applied &&
                (outcome.rollback_attempted ||
                 outcome.rollback_error != RemediationError::None ||
                 !outcome.rollback_detail.empty())) {
                return false;
            }
        }
        return true;
    };

    if (!isValidUtf8(record.detail)) return false;

    switch (record.status) {
    case TransactionStatus::Unprepared:
        return false;
    case TransactionStatus::Prepared:
        return record.error == RemediationError::None && applied_count == 0 &&
               record.action_order.empty() && all_not_checked_after_prefix() &&
               no_rollback_failure_metadata();
    case TransactionStatus::Applying:
        return record.error == RemediationError::None && applied_count > 0 &&
               applied_count < record.prepared_actions.size() &&
               unresolved_count == applied_count && apply_order_is_exact() &&
               all_not_checked_after_prefix() && no_rollback_failure_metadata();
    case TransactionStatus::Applied:
        return record.error == RemediationError::None &&
               applied_count == record.prepared_actions.size() &&
               unresolved_count == applied_count && apply_order_is_exact() &&
               no_rollback_failure_metadata();
    case TransactionStatus::Failed: {
        if (record.error == RemediationError::None ||
            record.error == RemediationError::Cancelled ||
            record.error == RemediationError::RollbackFailed ||
            unresolved_count != applied_count || !apply_order_is_exact() ||
            !valid_suffix() || !no_rollback_failure_metadata()) return false;
        const bool has_failed_outcome =
            applied_count < record.outcomes.size() &&
            record.outcomes[applied_count].status == ActionStatus::Failed;
        if (!has_failed_outcome) return false;
        const auto& failed = record.outcomes[applied_count];
        return record.error == failed.error && record.detail == failed.detail;
    }
    case TransactionStatus::Cancelled: {
        if (record.error != RemediationError::Cancelled || !valid_suffix()) return false;
        const bool has_failed_outcome =
            applied_count < record.outcomes.size() &&
            record.outcomes[applied_count].status == ActionStatus::Failed;
        const bool cancelled_during_apply =
            unresolved_count == applied_count && apply_order_is_exact() &&
            no_rollback_failure_metadata() &&
            (!has_failed_outcome ||
             (record.outcomes[applied_count].error == RemediationError::Cancelled &&
              record.detail == record.outcomes[applied_count].detail));
        const bool cancelled_during_rollback =
            applied_count > 0 && unresolved_count > 0 &&
            rollback_order_is_exact(true) &&
            rollback_failure_metadata_is_exact();
        return cancelled_during_apply || cancelled_during_rollback;
    }
    case TransactionStatus::RollingBack:
        return record.error == RemediationError::None && applied_count > 0 &&
               unresolved_count > 0 && valid_suffix() &&
               rollback_order_is_exact(true) &&
               rollback_failure_metadata_is_exact();
    case TransactionStatus::RollbackFailed:
        if (applied_count == 0 || !valid_suffix() ||
            !rollback_order_is_exact(true)) return false;
        if (record.error != RemediationError::RollbackFailed || unresolved_count == 0) {
            return false;
        }
        {
            const auto& failed = record.outcomes[unresolved_count - 1];
            return rollback_failure_metadata_is_exact() &&
                   failed.rollback_attempted &&
                   failed.rollback_error != RemediationError::None &&
                   record.detail == failed.rollback_detail;
        }
    case TransactionStatus::Reverted:
        return record.error == RemediationError::None && applied_count > 0 &&
               unresolved_count == 0 && valid_suffix() &&
               rollback_order_is_exact(false);
    }
    return false;
}

std::optional<TransactionRecord> readRecord(const json& value, std::string_view expected_id) {
    try {
        if (!value.is_object()) return std::nullopt;
        uint64_t schema_version = 0;
        TransactionStatus status;
        RemediationError error;
        std::string transaction_id;
        std::string detail;
        if (!isUnsigned(value.at("schema_version"), kBackupVersion, schema_version) ||
            schema_version != kBackupVersion ||
            !isString(value.at("transaction_id"), kMaxTransactionIdLength, transaction_id) ||
            transaction_id != expected_id || !isTransactionId(transaction_id) ||
            !readEnum(value.at("status"), status, 9) ||
            !readEnum(value.at("error"), error, 14) ||
            !isString(value.at("detail"), kMaxDetailLength, detail)) return std::nullopt;

        const auto& prepared_json = value.at("prepared_actions");
        const auto& outcomes_json = value.at("outcomes");
        if (!prepared_json.is_array() || prepared_json.size() > kMaxTransactionActions ||
            !outcomes_json.is_array() || outcomes_json.size() != prepared_json.size()) {
            return std::nullopt;
        }
        TransactionRecord record;
        record.schema_version = static_cast<uint32_t>(schema_version);
        record.transaction_id = std::move(transaction_id);
        record.status = status;
        record.error = error;
        record.detail = std::move(detail);
        for (const auto& item : prepared_json) {
            const auto action = readPreparedAction(item);
            if (!action) return std::nullopt;
            record.prepared_actions.push_back(*action);
        }
        for (const auto& item : outcomes_json) {
            const auto outcome = readOutcome(item);
            if (!outcome) return std::nullopt;
            record.outcomes.push_back(*outcome);
        }
        const auto action_order = readActionOrder(value.at("action_order"), 2 * kMaxTransactionActions);
        const auto applied_order = readActionOrder(value.at("applied_action_order"),
                                                   kMaxTransactionActions);
        if (!action_order || !applied_order) return std::nullopt;
        record.action_order = *action_order;
        record.applied_action_order = *applied_order;
        return validRecordShape(record) ? std::optional<TransactionRecord>{std::move(record)}
                                        : std::nullopt;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

std::optional<TransactionRecord> parseStoredRecord(
    const std::string& content, std::string_view expected_id) {
    try {
        const auto document = json::parse(content, nullptr, false);
        if (document.is_discarded() || !document.is_object() ||
            !document.contains("version") || !document.contains("producer") ||
            !document.contains("transaction")) {
            return std::nullopt;
        }
        uint64_t version = 0;
        std::string producer;
        if (!isUnsigned(document.at("version"), kBackupVersion, version) ||
            version != kBackupVersion ||
            !isString(document.at("producer"), sizeof(kProducer) - 1, producer) ||
            producer != kProducer) {
            return std::nullopt;
        }
        return readRecord(document.at("transaction"), expected_id);
    } catch (const std::exception&) {
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool isResolved(const TransactionRecord& record) noexcept {
    return record.status == TransactionStatus::Reverted;
}

bool sameRollbackPlan(const TransactionRecord& left, const TransactionRecord& right) {
    if (left.transaction_id != right.transaction_id ||
        left.prepared_actions.size() != right.prepared_actions.size()) return false;
    for (std::size_t index = 0; index < left.prepared_actions.size(); ++index) {
        const auto& a = left.prepared_actions[index];
        const auto& b = right.prepared_actions[index];
        if (a.id != b.id || a.target != b.target || a.before != b.before ||
            a.proposed != b.proposed || a.rollback_supported != b.rollback_supported) return false;
    }
    return true;
}

std::size_t unresolvedAppliedCount(const TransactionRecord& record) noexcept {
    std::size_t count = 0;
    while (count < record.applied_action_order.size() &&
           record.outcomes[count].status == ActionStatus::Applied) {
        ++count;
    }
    return count;
}

bool sameRecord(const TransactionRecord& left, const TransactionRecord& right) {
    try {
        return writeRecord(left) == writeRecord(right);
    } catch (...) {
        return false;
    }
}

bool legalSuccessor(
    const TransactionRecord& predecessor,
    const TransactionRecord& successor) noexcept {
    if (!sameRollbackPlan(predecessor, successor)) return false;
    if (sameRecord(predecessor, successor)) return true;

    const auto previous_applied = predecessor.applied_action_order.size();
    const auto next_applied = successor.applied_action_order.size();
    if (next_applied < previous_applied) return false;

    const auto previous_unresolved = unresolvedAppliedCount(predecessor);
    const auto next_unresolved = unresolvedAppliedCount(successor);
    const auto next = successor.status;
    switch (predecessor.status) {
    case TransactionStatus::Prepared:
        return next == TransactionStatus::Applying ||
               next == TransactionStatus::Applied ||
               next == TransactionStatus::Failed ||
               next == TransactionStatus::Cancelled;
    case TransactionStatus::Applying:
        if (next == TransactionStatus::Applying) {
            return next_applied > previous_applied;
        }
        return next_applied >= previous_applied &&
               (next == TransactionStatus::Applied ||
                next == TransactionStatus::Failed ||
                next == TransactionStatus::Cancelled);
    case TransactionStatus::Applied:
        return next == TransactionStatus::RollingBack &&
               next_applied == previous_applied;
    case TransactionStatus::Failed:
    case TransactionStatus::Cancelled:
        return previous_unresolved > 0 &&
               next == TransactionStatus::RollingBack &&
               next_applied == previous_applied;
    case TransactionStatus::RollingBack:
        if (next_applied != previous_applied ||
            next_unresolved > previous_unresolved) {
            return false;
        }
        if (next == TransactionStatus::RollingBack) {
            return next_unresolved < previous_unresolved;
        }
        return next == TransactionStatus::RollbackFailed ||
               next == TransactionStatus::Cancelled ||
               next == TransactionStatus::Reverted;
    case TransactionStatus::RollbackFailed:
        return next == TransactionStatus::RollingBack &&
               next_applied == previous_applied &&
               next_unresolved <= previous_unresolved;
    case TransactionStatus::Reverted:
    case TransactionStatus::Unprepared:
        return false;
    }
    return false;
}

} // namespace

JsonBackupStore::JsonBackupStore(
    std::filesystem::path storage_root, FailureInjector failure_injector)
    : storage_root_(std::move(storage_root)),
      failure_injector_(std::move(failure_injector)) {}

Result<std::monostate> JsonBackupStore::save(const TransactionRecord& record) {
    if (!validRecordShape(record)) return backupFailure<std::monostate>("invalid transaction record");

    std::string content;
    try {
        const json document = {{"version", kBackupVersion}, {"producer", kProducer},
                               {"transaction", writeRecord(record)}};
        content = document.dump();
    } catch (const std::exception&) {
        return backupFailure<std::monostate>("could not serialize transaction backup");
    } catch (...) {
        return backupFailure<std::monostate>("could not serialize transaction backup");
    }
    if (content.size() > kMaxTransactionBytes) {
        return backupFailure<std::monostate>("transaction backup exceeds 256 KiB");
    }

    std::lock_guard<std::mutex> process_lock(backupStoreMutex());
    bool missing = false;
    auto directory = TransactionDirectory::open(
        transactionsRoot(storage_root_), true, failure_injector_, missing);
    if (!directory || !directory->lock()) {
        return backupFailure<std::monostate>(
            "could not securely open or lock transaction storage");
    }

    const auto name = record.transaction_id + ".json";
    const auto predecessor = directory->read(name, kMaxTransactionBytes);
    std::optional<FileIdentity> expected;
    std::optional<StoredFile> previous_file;
    if (predecessor.status == ReadStatus::Failure) {
        return backupFailure<std::monostate>(
            "could not inspect existing transaction backup");
    }
    if (predecessor.status == ReadStatus::Success) {
        const auto existing = parseStoredRecord(
            predecessor.file.content, record.transaction_id);
        if (!existing) {
            return backupFailure<std::monostate>(
                "refusing to overwrite an unrecognized transaction backup");
        }
        if (!legalSuccessor(*existing, record)) {
            return backupFailure<std::monostate>(
                "refusing a non-monotonic transaction transition");
        }
        expected = predecessor.file.identity;
        previous_file = predecessor.file;
    }

    const auto rollback_installed_write = [&]() {
        const auto current = directory->read(name, kMaxTransactionBytes);
        if (current.status != ReadStatus::Success ||
            current.file.content != content) {
            return false;
        }
        if (previous_file) {
            return directory->write(name, previous_file->content,
                                    current.file.identity);
        }
        return directory->remove(name, current.file.identity);
    };

    if (!directory->write(name, content, expected)) {
        (void)rollback_installed_write();
        return backupFailure<std::monostate>("could not write transaction backup");
    }

    const auto rollback_new_write = [&]() {
        return rollback_installed_write();
    };

    std::vector<std::pair<FileIdentity, std::string>> resolved;
    std::vector<DirectoryEntry> entries;
    if (!directory->entries(entries)) {
        (void)rollback_new_write();
        return backupFailure<std::monostate>(
            "could not enumerate transaction backups for retention");
    }
    for (const auto& entry : entries) {
        const std::filesystem::path entry_path(entry.name);
        if (entry_path.extension() != ".json") continue;
        const auto id = entry_path.stem().string();
        if (!isTransactionId(id)) continue;
        const auto loaded_file = directory->read(entry.name, kMaxTransactionBytes);
        if (loaded_file.status != ReadStatus::Success) {
            (void)rollback_new_write();
            return backupFailure<std::monostate>(
                "could not inspect transaction backup metadata for retention");
        }
        const auto loaded = parseStoredRecord(loaded_file.file.content, id);
        if (!loaded || !isResolved(*loaded)) continue;
        resolved.emplace_back(loaded_file.file.identity, entry.name);
    }
    std::sort(resolved.begin(), resolved.end(), [](const auto& left, const auto& right) {
        if (left.first.modified_seconds != right.first.modified_seconds) {
            return left.first.modified_seconds < right.first.modified_seconds;
        }
        if (left.first.modified_fraction != right.first.modified_fraction) {
            return left.first.modified_fraction < right.first.modified_fraction;
        }
        return left.second < right.second;
    });
    while (resolved.size() > kMaxResolvedTransactions) {
        if (!directory->remove(resolved.front().second,
                               resolved.front().first)) {
            (void)rollback_new_write();
            return backupFailure<std::monostate>(
                "transaction backup retention pruning failed");
        }
        resolved.erase(resolved.begin());
    }
    return backupSuccess();
}

Result<TransactionRecord> JsonBackupStore::load(std::string_view transaction_id) const {
    if (!isTransactionId(transaction_id)) return backupFailure<TransactionRecord>("invalid transaction id");
    std::lock_guard<std::mutex> process_lock(backupStoreMutex());
    bool missing = false;
    auto directory = TransactionDirectory::open(
        transactionsRoot(storage_root_), false, failure_injector_, missing);
    if (!directory || !directory->lock()) {
        return backupFailure<TransactionRecord>(
            "transaction backup storage is unavailable or unsafe");
    }
    const auto file = directory->read(
        std::string(transaction_id) + ".json", kMaxTransactionBytes);
    if (file.status != ReadStatus::Success) {
        return backupFailure<TransactionRecord>(
            "transaction backup is unavailable or too large");
    }
    const auto record = parseStoredRecord(file.file.content, transaction_id);
    if (!record) {
        return backupFailure<TransactionRecord>(
            "transaction backup failed validation");
    }
    Result<TransactionRecord> result;
    result.value = *record;
    result.error = RemediationError::None;
    return result;
}

Result<std::vector<TransactionSummary>> JsonBackupStore::list() const {
    std::vector<TransactionSummary> summaries;
    std::lock_guard<std::mutex> process_lock(backupStoreMutex());
    bool missing = false;
    auto directory = TransactionDirectory::open(
        transactionsRoot(storage_root_), false, failure_injector_, missing);
    if (!directory) {
        Result<std::vector<TransactionSummary>> result;
        result.value = std::move(summaries);
        result.error = missing ? RemediationError::None
                               : RemediationError::BackupFailed;
        result.detail = missing ? "" : "could not securely enumerate transaction backups";
        return result;
    }
    if (!directory->lock()) {
        return backupFailure<std::vector<TransactionSummary>>(
            "could not lock transaction backups");
    }
    std::vector<DirectoryEntry> entries;
    if (!directory->entries(entries)) {
        return backupFailure<std::vector<TransactionSummary>>(
            "could not enumerate transaction backups");
    }
    for (const auto& entry : entries) {
        const std::filesystem::path entry_path(entry.name);
        if (entry_path.extension() != ".json") continue;
        const auto id = entry_path.stem().string();
        if (!isTransactionId(id)) continue;
        const auto file = directory->read(entry.name, kMaxTransactionBytes);
        if (file.status != ReadStatus::Success) continue;
        const auto record = parseStoredRecord(file.file.content, id);
        if (record) summaries.push_back({record->transaction_id, record->status});
    }
    std::sort(summaries.begin(), summaries.end(), [](const auto& left, const auto& right) {
        return left.transaction_id < right.transaction_id;
    });
    Result<std::vector<TransactionSummary>> result;
    result.value = std::move(summaries);
    result.error = RemediationError::None;
    return result;
}

} // namespace gno
