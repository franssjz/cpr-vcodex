#include "SudokuPuzzleBank.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>

namespace {

constexpr char kModule[] = "SUDO";
constexpr size_t kPuzzleLength = 81;
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

bool isPuzzleChar(char c) { return (c >= '0' && c <= '9') || c == '.' || c == '-'; }

// Extracts the 81-character puzzle field from a record line. The line may be
// either a bare 81-char puzzle or a whitespace-separated record whose puzzle
// is the one token of length 81. Returns false if no such token exists.
bool extractPuzzle(const std::string& line, std::string& outPuzzle) {
  size_t start = 0;
  while (start < line.size()) {
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) start++;
    size_t end = start;
    while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) end++;

    if (end - start == kPuzzleLength) {
      const std::string token = line.substr(start, kPuzzleLength);
      if (std::all_of(token.begin(), token.end(), isPuzzleChar)) {
        outPuzzle = token;
        return true;
      }
    }
    start = end;
  }
  return false;
}

bool hasTxtExtension(const std::string& name) {
  if (name.size() < 4) return false;
  std::string ext = name.substr(name.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return ext == ".txt";
}

}  // namespace

std::vector<std::string> SudokuPuzzleBank::listBankPaths(const std::string& directory) {
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

std::string SudokuPuzzleBank::displayNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

bool SudokuPuzzleBank::open(const std::string& path) {
  close();

  FsFile file;
  if (!Storage.openFileForRead(kModule, path, file)) {
    LOG_DBG(kModule, "Could not open puzzle bank: %s", path.c_str());
    return false;
  }

  const size_t fileSize = file.size();

  // The stride is the byte length of the first record including its line
  // terminator -- this assumes the fixed-width layout the format guarantees.
  std::string firstLine;
  const bool gotFirst = readLine(file, firstLine);
  const size_t firstLineEnd = file.position();
  file.close();

  std::string puzzle;
  if (!gotFirst || fileSize == 0 || !extractPuzzle(firstLine, puzzle)) {
    LOG_DBG(kModule, "No valid puzzles in bank: %s", path.c_str());
    return false;
  }

  const uint32_t stride = static_cast<uint32_t>(firstLineEnd);
  if (stride < kPuzzleLength) return false;

  // Whole records, plus a final line that may lack a trailing newline.
  int count = static_cast<int>(fileSize / stride);
  if (fileSize % stride >= kPuzzleLength) count++;
  if (count <= 0) return false;

  path_ = path;
  stride_ = stride;
  puzzleCount_ = count;
  return true;
}

void SudokuPuzzleBank::close() {
  path_.clear();
  stride_ = 0;
  puzzleCount_ = 0;
}

bool SudokuPuzzleBank::loadPuzzle(int index, std::string& outPuzzle) const {
  outPuzzle.clear();
  if (index < 0 || index >= puzzleCount_ || stride_ == 0) return false;

  FsFile file;
  if (!Storage.openFileForRead(kModule, path_, file)) {
    LOG_DBG(kModule, "Could not reopen puzzle bank: %s", path_.c_str());
    return false;
  }

  if (!file.seekSet(static_cast<size_t>(index) * stride_)) {
    file.close();
    return false;
  }

  std::string line;
  const bool gotLine = readLine(file, line);
  file.close();

  if (!gotLine) return false;
  return extractPuzzle(line, outPuzzle);
}
