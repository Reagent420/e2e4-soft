#include "json_persistence.h"

#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::filesystem::path environmentPath(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
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

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file) {
            file.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    if (replaceFile(temporary, path)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}

} // namespace gno::persistence
