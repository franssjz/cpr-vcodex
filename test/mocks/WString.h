// Mock Arduino String class for native testing
#pragma once

#include <cstring>
#include <string>

class String {
  std::string _data;

 public:
  String() = default;
  String(const char* s) : _data(s ? s : "") {}
  String(const std::string& s) : _data(s) {}
  bool isEmpty() const { return _data.empty(); }
  const char* c_str() const { return _data.c_str(); }
  size_t length() const { return _data.size(); }
  bool operator==(const char* s) const { return _data == s; }
  bool operator!=(const char* s) const { return _data != s; }
};
