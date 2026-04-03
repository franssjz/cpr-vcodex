// Mock HardwareSerial.h for native testing
#pragma once

#include "Print.h"

class HWCDC : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return true; }
  size_t write(uint8_t) override { return 1; }
};
