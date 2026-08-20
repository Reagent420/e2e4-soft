#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace gno {

std::optional<int> parseBoundedInt(const std::string& text, int minimum, int maximum);
std::optional<std::string> readBoundedFile(const std::string& path, std::size_t max_bytes);

} // namespace gno
