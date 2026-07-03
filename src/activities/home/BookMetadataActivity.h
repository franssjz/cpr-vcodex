#pragma once
#include <string>
#include <vector>

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"

class Epub;

class BookMetadataActivity final : public Activity {
 public:
  explicit BookMetadataActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& bookPath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MetadataLine {
    std::string label;
    std::string value;
  };

  std::string bookPath;
  std::vector<MetadataLine> lines;
  std::vector<std::string> descriptionLines;
  int scrollOffset = 0;
  int maxLines = 0;

  void loadMetadata();
};