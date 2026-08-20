#include "input_validation.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace gno {

std::optional<int> parseBoundedInt(const std::string& text, int minimum, int maximum) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    if (value < minimum || value > maximum) return std::nullopt;
    return value;
}

std::optional<std::string> readBoundedRegularFile(const std::filesystem::path& path,
                                                  std::size_t max_bytes) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool regular = GetFileInformationByHandle(handle, &info) != 0 &&
                         (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                         (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                         info.nFileSizeHigh == 0 && info.nFileSizeLow <= max_bytes;
    if (!regular) { CloseHandle(handle); return std::nullopt; }
    std::string result(info.nFileSizeLow, '\0');
    DWORD offset = 0;
    while (offset < result.size()) { DWORD count = 0; if (!ReadFile(handle, result.data()+offset, static_cast<DWORD>(result.size()-offset), &count, nullptr) || count == 0) { CloseHandle(handle); return std::nullopt; } offset += count; }
    CloseHandle(handle); return result;
#else
    struct stat before{};
    if (lstat(path.c_str(), &before) != 0 || !S_ISREG(before.st_mode)) return std::nullopt;
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) return std::nullopt;
    struct stat info{};
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_dev != before.st_dev || info.st_ino != before.st_ino || info.st_size < 0 ||
        static_cast<std::uintmax_t>(info.st_size) > max_bytes) { close(descriptor); return std::nullopt; }
    std::string result(static_cast<std::size_t>(info.st_size), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) { const auto count = read(descriptor, result.data()+offset, result.size()-offset); if (count <= 0) { close(descriptor); return std::nullopt; } offset += static_cast<std::size_t>(count); }
    close(descriptor); return result;
#endif
}

std::optional<std::string> readBoundedFile(const std::string& path, std::size_t max_bytes) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    const auto end = file.tellg();
    if (end < 0) return std::nullopt;

    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(max_bytes) ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        size > static_cast<std::uintmax_t>(std::string{}.max_size())) {
        return std::nullopt;
    }

    std::string content;
    try {
        content.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (const std::length_error&) {
        return std::nullopt;
    }
    file.seekg(0);
    if (!file) return std::nullopt;
    if (!content.empty() && !file.read(content.data(), static_cast<std::streamsize>(content.size()))) {
        return std::nullopt;
    }
    if (file.peek() != std::char_traits<char>::eof() || file.bad()) return std::nullopt;
    return content;
}

} // namespace gno
