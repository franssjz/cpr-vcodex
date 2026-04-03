// Mock HalStorage.h for native testing
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "Print.h"
#include "WString.h"
#include "common/FsApiConstants.h"
#include "freertos/semphr.h"

class HalFile : public Print {
 public:
  HalFile() = default;
  ~HalFile() override = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush() override {}
  size_t getName(char*, size_t) { return 0; }
  size_t size() { return 0; }
  size_t fileSize() { return 0; }
  bool seek(size_t) { return false; }
  bool seekCur(int64_t) { return false; }
  bool seekSet(size_t) { return false; }
  int available() const { return 0; }
  size_t position() const { return 0; }
  int read(void*, size_t) { return 0; }
  int read() { return -1; }
  size_t write(const void*, size_t) { return 0; }
  size_t write(uint8_t) override { return 1; }
  bool rename(const char*) { return false; }
  bool isDirectory() const { return false; }
  void rewindDirectory() {}
  bool close() { return true; }
  HalFile openNextFile() { return HalFile(); }
  bool isOpen() const { return false; }
  operator bool() const { return false; }
};

#ifndef HAL_STORAGE_IMPL
using FsFile = HalFile;
#endif

class HalStorage {
 public:
  HalStorage() = default;
  bool begin() { return true; }
  bool ready() const { return true; }
  String readFile(const char*) { return String(""); }
  bool writeFile(const char*, const String&) { return true; }
  bool ensureDirectoryExists(const char*) { return true; }
  HalFile open(const char*, oflag_t = O_RDONLY) { return HalFile(); }
  bool mkdir(const char*, bool = true) { return true; }
  bool exists(const char*) { return false; }
  bool remove(const char*) { return false; }
  bool rename(const char*, const char*) { return false; }
  bool rmdir(const char*) { return false; }
  bool openFileForRead(const char*, const char*, HalFile&) { return false; }
  bool openFileForRead(const char*, const std::string&, HalFile&) { return false; }
  bool openFileForRead(const char*, const String&, HalFile&) { return false; }
  bool openFileForWrite(const char*, const char*, HalFile&) { return false; }
  bool openFileForWrite(const char*, const std::string&, HalFile&) { return false; }
  bool openFileForWrite(const char*, const String&, HalFile&) { return false; }
  bool removeDir(const char*) { return false; }

  static HalStorage& getInstance() { return instance; }

 private:
  static HalStorage instance;
};

inline HalStorage HalStorage::instance;

#define Storage HalStorage::getInstance()

#ifdef SdMan
#undef SdMan
#endif
