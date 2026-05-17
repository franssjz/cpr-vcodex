#pragma once

#include <cstddef>
#include <string>

namespace IfFoundFile {
constexpr const char* DEFAULT_PATH = "/if_found.txt";
constexpr size_t MAX_BYTES = 8192;

std::string findPath();
std::string readNormalized(const std::string& path);
}  // namespace IfFoundFile
