#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

inline size_t testWriteBudget = std::numeric_limits<size_t>::max();
inline bool testRenameFailure = false;
inline bool testCloseFailure = false;

class HalFile {
 public:
  std::fstream stream;
  std::filesystem::path path;
  int read(void* data, size_t size) {
    stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<int>(stream.gcount());
  }
  size_t write(const void* data, size_t size) {
    const size_t count = std::min(size, testWriteBudget);
    testWriteBudget -= count;
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
    return stream ? count : 0;
  }
  size_t fileSize() const { return static_cast<size_t>(std::filesystem::file_size(path)); }
  void flush() { stream.flush(); }
  bool close() {
    stream.close();
    return !stream.fail() && !testCloseFailure;
  }
};

class HalStorage {
 public:
  std::filesystem::path root;
  std::filesystem::path resolve(const char* path) const { return root / (path + 1); }
  bool mkdir(const char* path) {
    std::filesystem::create_directories(resolve(path));
    return true;
  }
  bool exists(const char* path) const { return std::filesystem::exists(resolve(path)); }
  bool remove(const char* path) { return std::filesystem::remove(resolve(path)); }
  bool rename(const char* from, const char* to) {
    if (testRenameFailure) return false;
    std::filesystem::rename(resolve(from), resolve(to));
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file.path = resolve(path);
    file.stream.open(file.path, std::ios::binary | std::ios::out | std::ios::trunc);
    return file.stream.is_open();
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file.path = resolve(path);
    file.stream.open(file.path, std::ios::binary | std::ios::in);
    return file.stream.is_open();
  }
};
inline HalStorage Storage;
