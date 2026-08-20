#include "input_validation.h"

#include <charconv>
#include <fstream>

namespace gno {

std::optional<int> parseBoundedInt(const std::string& text, int minimum, int maximum) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    if (value < minimum || value > maximum) return std::nullopt;
    return value;
}

std::optional<std::string> readBoundedFile(const std::string& path, std::size_t max_bytes) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    const auto end = file.tellg();
    if (end < 0 || static_cast<std::size_t>(end) > max_bytes) return std::nullopt;

    std::string content(static_cast<std::size_t>(end), '\0');
    file.seekg(0);
    if (!content.empty() && !file.read(content.data(), static_cast<std::streamsize>(content.size()))) {
        return std::nullopt;
    }
    return content;
}

} // namespace gno
