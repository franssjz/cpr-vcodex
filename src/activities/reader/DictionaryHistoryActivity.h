#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class DictionaryHistoryActivity final : public Activity {
 public:
  DictionaryHistoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                            int readerFontId, int marginLeft, int marginTop)
      : Activity("DictionaryHistory", renderer, mappedInput),
        page(std::move(page)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::shared_ptr<Page> page;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  std::vector<std::string> history;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void lookupSelected();
};
