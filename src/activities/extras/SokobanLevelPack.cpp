#include "SokobanLevelPack.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr char kModule[] = "SOKO";
constexpr size_t kMaxLineLength = 256;

// Reads one line (without the trailing '\n'/'\r') from an open file.
// Returns false at EOF when nothing was read.
bool readLine(FsFile& file, std::string& outLine) {
  outLine.clear();
  int c = file.read();
  if (c < 0) return false;
  while (c >= 0 && c != '\n') {
    if (c != '\r' && outLine.size() < kMaxLineLength) {
      outLine.push_back(static_cast<char>(c));
    }
    c = file.read();
  }
  return true;
}

bool isBlankLine(const std::string& line) { return line.find_first_not_of(" \t") == std::string::npos; }

bool isCommentLine(const std::string& line) { return !line.empty() && line.front() == ';'; }

bool hasTxtExtension(const std::string& name) {
  if (name.size() < 4) return false;
  std::string ext = name.substr(name.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return ext == ".txt";
}

}  // namespace

std::vector<std::string> SokobanLevelPack::listPackPaths(const std::string& directory) {
  std::vector<std::string> paths;

  auto dir = Storage.open(directory.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return paths;
  }

  dir.rewindDirectory();
  char name[256];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    if (!file.isDirectory()) {
      const std::string filename(name);
      if (hasTxtExtension(filename)) {
        paths.push_back(directory + "/" + filename);
      }
    }
    file.close();
  }
  dir.close();

  std::sort(paths.begin(), paths.end());
  return paths;
}

std::string SokobanLevelPack::displayNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

bool SokobanLevelPack::open(const std::string& path) {
  close();

  FsFile file;
  if (!Storage.openFileForRead(kModule, path, file)) {
    LOG_DBG(kModule, "Could not open level pack: %s", path.c_str());
    return false;
  }

  // Walk the file once, recording the byte offset of the first content line
  // of every level (a non-blank, non-comment line that follows a blank line
  // or the start of the file).
  bool atLevelBoundary = true;
  bool insideLevel = false;
  std::string line;
  while (true) {
    const size_t offsetBeforeLine = file.position();
    if (!readLine(file, line)) break;

    if (isBlankLine(line)) {
      atLevelBoundary = true;
      insideLevel = false;
      continue;
    }
    if (isCommentLine(line)) {
      continue;
    }

    if (atLevelBoundary && !insideLevel) {
      levelOffsets_.push_back(static_cast<uint32_t>(offsetBeforeLine));
      insideLevel = true;
    }
    atLevelBoundary = false;
  }

  file.close();

  if (levelOffsets_.empty()) {
    LOG_DBG(kModule, "No levels found in pack: %s", path.c_str());
    return false;
  }

  path_ = path;
  return true;
}

void SokobanLevelPack::close() {
  path_.clear();
  levelOffsets_.clear();
}

bool SokobanLevelPack::loadLevel(int index, std::vector<std::string>& outLines) const {
  outLines.clear();
  if (index < 0 || index >= levelCount()) return false;

  FsFile file;
  if (!Storage.openFileForRead(kModule, path_, file)) {
    LOG_DBG(kModule, "Could not reopen level pack: %s", path_.c_str());
    return false;
  }

  if (!file.seekSet(levelOffsets_[static_cast<size_t>(index)])) {
    file.close();
    return false;
  }

  std::string line;
  while (readLine(file, line)) {
    if (isBlankLine(line)) break;
    if (isCommentLine(line)) continue;
    outLines.push_back(line);
  }

  file.close();
  return !outLines.empty();
}
