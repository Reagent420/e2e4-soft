#include "input_validation.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>

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
