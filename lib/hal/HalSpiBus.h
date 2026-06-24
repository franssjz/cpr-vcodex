#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class HalSpiBus {
 public:
  class Lock {
   public:
    Lock();
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

   private:
    bool acquired = false;
  };

  static HalSpiBus& getInstance();

 private:
  HalSpiBus();

  SemaphoreHandle_t mutex = nullptr;

  friend class Lock;
};
