#pragma once

#include <string>

namespace CompletedBookMover {

struct MoveResult {
  bool attempted = false;
  bool moved = false;
  std::string sourcePath;
  std::string destinationPath;
};

MoveResult moveCompletedBookIfEnabled(const std::string& sourcePath, const std::string& title = "",
                                      const std::string& author = "", const std::string& coverBmpPath = "",
                                      const std::string& bookId = "");

}  // namespace CompletedBookMover
