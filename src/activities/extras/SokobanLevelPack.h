#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reads Sokoban level packs in plain XSB text format directly from the SD
// card (e.g. /sokoban/levels/Microban.txt). Levels are separated by blank
// lines; ';' lines are comments. Rather than holding a whole pack in RAM,
// open() builds a small index of byte offsets so loadLevel() can seek
// straight to the requested level and read only its lines.
class SokobanLevelPack {
 public:
  // Scans the SD card directory for .txt level packs.
  static std::vector<std::string> listPackPaths(const std::string& directory);

  // Builds a display name from a pack file path (file name without extension).
  static std::string displayNameFromPath(const std::string& path);

  bool open(const std::string& path);
  void close();

  bool isOpen() const { return !levelOffsets_.empty(); }
  int levelCount() const { return static_cast<int>(levelOffsets_.size()); }
  const std::string& path() const { return path_; }

  // Reads the lines of the given level (0-based index) into outLines.
  bool loadLevel(int index, std::vector<std::string>& outLines) const;

 private:
  std::string path_;
  std::vector<uint32_t> levelOffsets_;
};
