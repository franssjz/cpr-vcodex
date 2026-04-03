// Mock Arduino.h for native testing
#pragma once

#include <cstddef>
#include <cstdint>

#include "Print.h"
#include "WString.h"

// FreeRTOS stubs
#define portTICK_PERIOD_MS 1
inline void vTaskDelay(uint32_t) {}

// Arduino core stubs
inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

// ESP class stub
class EspClass {
 public:
  uint32_t getFreeHeap() { return 380000; }
  void restart() {}
};
inline EspClass ESP;
