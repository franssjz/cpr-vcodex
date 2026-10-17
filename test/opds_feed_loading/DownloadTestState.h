#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct DownloadTestState {
  std::map<std::string, std::string> files;
  std::vector<std::string> chunks{"first chunk", "second chunk"};
  std::vector<std::string> events;
  std::string url;
  std::string username;
  std::string password;
  std::vector<std::pair<std::string, std::string>> headers;
  const bool* cancelFlag = nullptr;
  int result = 0;
  int transfers = 0;
  int openFiles = 0;
  int closeCalls = 0;
  int writeCalls = 0;
  int partialWriteAt = -1;
  bool openOk = true;
  bool closeOk = true;
  bool downgrade = false;
  bool removeWhileOpen = false;
};
inline DownloadTestState downloadTest;
