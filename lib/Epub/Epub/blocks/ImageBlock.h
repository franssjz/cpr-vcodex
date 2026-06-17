#pragma once
#include <HalStorage.h>

#include <memory>
#include <string>

#include "Block.h"

class ImageBlock final : public Block {
 public:
  ImageBlock(const std::string& imagePath, int16_t width, int16_t height);
  ImageBlock(const std::string& imagePath, int16_t width, int16_t height, std::string sourceEpubPath,
             std::string sourceItemHref);
  ~ImageBlock() override = default;

  const std::string& getImagePath() const { return imagePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }

  bool imageExists() const;

  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }

  void render(GfxRenderer& renderer, const int x, const int y);
  bool serialize(FsFile& file);
  static std::unique_ptr<ImageBlock> deserialize(FsFile& file);

 private:
  std::string imagePath;
  std::string sourceEpubPath;
  std::string sourceItemHref;
  int16_t width;
  int16_t height;

  bool extractLazyImageIfNeeded();
};
