#pragma once

#include <string>
#include <vector>

namespace SleepImageUtils {

bool isSleepDirectoryName(const std::string& name);
std::vector<std::string> listSleepDirectories();
std::vector<std::string> listImageFiles(const std::string& directoryPath);
std::vector<std::string> listBmpFiles(const std::string& directoryPath);
std::string resolveConfiguredSleepDirectory();
std::string getDirectoryLabel(const std::string& directoryPath);

}  // namespace SleepImageUtils
