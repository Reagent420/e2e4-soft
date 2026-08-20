#pragma once

#include <filesystem>
#include <string>

namespace gno::persistence {

std::filesystem::path applicationDataRoot();
std::filesystem::path storageFile(const std::filesystem::path& storage_root,
                                  const std::string& filename);
bool atomicWriteText(const std::filesystem::path& path, const std::string& content);

} // namespace gno::persistence
