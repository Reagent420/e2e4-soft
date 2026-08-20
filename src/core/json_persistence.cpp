#include "json_persistence.h"

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path environmentPath(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

} // namespace

namespace gno::persistence {

std::filesystem::path applicationDataRoot() {
#ifdef _WIN32
    if (const auto app_data = environmentPath("APPDATA"); !app_data.empty()) return app_data;
#elif defined(__APPLE__)
    if (const auto home = environmentPath("HOME"); !home.empty()) {
        return home / "Library" / "Application Support";
    }
#else
    if (const auto xdg_data = environmentPath("XDG_DATA_HOME"); !xdg_data.empty()) return xdg_data;
    if (const auto home = environmentPath("HOME"); !home.empty()) return home / ".local" / "share";
#endif
    return std::filesystem::temp_directory_path();
}

std::filesystem::path storageFile(const std::filesystem::path& storage_root,
                                  const std::string& filename) {
    const auto root = storage_root.empty() ? applicationDataRoot() : storage_root;
    return root / "GNO" / filename;
}

bool atomicWriteText(const std::filesystem::path& path, const std::string& content) {
    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return false;
    }
#ifdef _WIN32
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = path; temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
    }
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool ok = true; std::size_t offset = 0;
    while (ok && offset < content.size()) { DWORD written = 0; const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(content.size()-offset, 1u << 20)); ok = WriteFile(handle, content.data()+offset, chunk, &written, nullptr) != 0 && written != 0; offset += written; }
    if (ok) ok = FlushFileBuffers(handle) != 0;
    const bool closed = CloseHandle(handle) != 0;
    ok = ok && closed;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
#else
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path temporary; int descriptor = -1;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = path; temporary += ".tmp." + std::to_string(getpid()) + "." + std::to_string(++sequence);
        descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor >= 0) break;
    }
    if (descriptor < 0) return false;
    bool ok = true; std::size_t offset = 0;
    while (ok && offset < content.size()) { const auto written = write(descriptor, content.data()+offset, content.size()-offset); ok = written > 0; if (ok) offset += static_cast<std::size_t>(written); }
    if (ok) ok = fsync(descriptor) == 0;
    const bool closed = close(descriptor) == 0;
    ok = ok && closed;
    if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
    const auto durable_parent = parent.empty() ? std::filesystem::path(".") : parent;
    const int directory = ok ? open(durable_parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    if (ok) { ok = directory >= 0 && fsync(directory) == 0; if (directory >= 0) close(directory); }
    if (!ok) unlink(temporary.c_str());
    return ok;
#endif
}

} // namespace gno::persistence
