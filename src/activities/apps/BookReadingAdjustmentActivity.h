#pragma once

#include <cstdint>
#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class BookReadingAdjustmentActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string bookPath;
  std::string bookTitle;
  int selectedField = 0;
  int selectedOperation = 0;
  int selectedDuration = 1;
  uint32_t selectedDayOrdinal = 0;
  bool lastApplyFailed = false;

  void adjustSelectedValue(int delta);
  void openDateSelection();
  void initializeSelectedDate();
  int32_t getSelectedDeltaMs() const;
  uint64_t getSelectedDayReadingMs() const;
  bool canApplySelectedAdjustment() const;
  std::string getAdjustmentPreviewInfo() const;
  const char* getOperationLabel() const;
  std::string getDateLabel() const;
  const char* getDurationLabel() const;
  bool applyAdjustment();

 public:
  explicit BookReadingAdjustmentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                         std::string bookTitle)
      : Activity("BookReadingAdjustment", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
