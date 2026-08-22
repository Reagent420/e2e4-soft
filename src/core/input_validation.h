#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace gno {

std::optional<int> parseBoundedInt(const std::string& text, int minimum, int maximum);
std::optional<std::string> readBoundedFile(const std::string& path, std::size_t max_bytes);
// Opens a regular file without following a link/reparse point and reads through that handle.
std::optional<std::string> readBoundedRegularFile(const std::filesystem::path& path,
                                                  std::size_t max_bytes);

} // namespace gno
