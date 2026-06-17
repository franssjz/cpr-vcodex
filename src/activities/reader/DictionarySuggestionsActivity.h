#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class DictionarySuggestionsActivity final : public Activity {
 public:
  DictionarySuggestionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                                std::string originalWord, std::vector<std::string> suggestions, int readerFontId,
                                int marginLeft, int marginTop)
      : Activity("DictionarySuggestions", renderer, mappedInput),
        page(std::move(page)),
        originalWord(std::move(originalWord)),
        suggestions(std::move(suggestions)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::shared_ptr<Page> page;
  std::string originalWord;
  std::vector<std::string> suggestions;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void lookupSelected();
};
