#pragma once
#include <cstdint>

#include "DownloadTestState.h"

class Stream;

class HalFile {
 public:
  std::string path;
  bool open = false;
  ~HalFile() {
    if (open) close();
  }
  size_t write(const uint8_t* data, size_t length) {
    ++downloadTest.writeCalls;
    const size_t count = downloadTest.writeCalls == downloadTest.partialWriteAt ? length / 2 : length;
    downloadTest.files[path].append(reinterpret_cast<const char*>(data), count);
    return count;
  }
  bool close() {
    ++downloadTest.closeCalls;
    downloadTest.events.emplace_back("close");
    if (open) {
      --downloadTest.openFiles;
      open = false;
    }
    return downloadTest.closeOk;
  }
};

struct DownloadStorage {
  bool exists(const char* path) const { return downloadTest.files.contains(path); }
  bool remove(const char* path) const {
    downloadTest.events.emplace_back("remove");
    downloadTest.removeWhileOpen |= downloadTest.openFiles != 0;
    return downloadTest.files.erase(path) != 0;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) const {
    if (!downloadTest.openOk) return false;
    file.path = path;
    file.open = true;
    ++downloadTest.openFiles;
    downloadTest.files[path].clear();
    return true;
  }
};
inline DownloadStorage Storage;
