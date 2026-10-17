#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct FeedTestState {
  std::map<std::string, std::string> files;
  std::vector<std::string> events;
  std::string response;
  std::string requestedUrl;
  std::string username;
  std::string password;
  std::string destination;
  int downloadResult = 0;
  int downloadCalls = 0;
  int parserCreations = 0;
  int liveParsers = 0;
  int openReaders = 0;
  int readCalls = 0;
  int readFailureAt = -1;
  int readFailureResult = -1;
  size_t maxRead = SIZE_MAX;
  size_t largestReadRequest = 0;
  int64_t reportedSize = -1;
  uint32_t freeHeap = 100000;
  uint32_t maxAlloc = 50000;
  uint32_t heapAfterFontRelease = 0;
  int renderLockDepth = 0;
  int watchdogResets = 0;
  int yields = 0;
  bool httpActive = false;
  bool parserDuringHttp = false;
  bool flushWithOpenFile = false;
  bool removeWithOpenFile = false;
  bool unlockedCacheRelease = false;
  bool unlockedCatalogRelease = false;
  bool waitWithRenderLock = false;
  bool staleEntriesAtDownload = false;
  bool failParserCreation = false;
  bool failBufferAllocation = false;
  bool directoryOk = true;
  bool removeOk = true;
  bool openOk = true;
  size_t catalogEntries = 0;
  size_t catalogCapacity = 0;
};

inline FeedTestState feedTest;
inline constexpr const char* feedTempPath = "/.crosspoint/opds-feed.tmp";
