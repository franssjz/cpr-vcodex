#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <PNGdec.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo.h"
#include "util/SleepImageUtils.h"
#include "util/PngSleepRenderer.h"
#include "util/SleepScreenCache.h"

namespace {
bool canUseSleepCache(const Bitmap& bitmap) {
  return !(bitmap.hasGreyscale() &&
           SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
}

bool usesCustomSleepImages() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
         (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM && !APP_STATE.lastSleepFromReader);
}
}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();
  const bool restoreDarkMode = renderer.isDarkMode();
  if (restoreDarkMode) {
    renderer.setDarkMode(false);
  }

  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    if (!usesCustomSleepImages()) {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    if (!usesCustomSleepImages()) {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    default:
      renderDefaultSleepScreen();
      break;
  }

  if (restoreDarkMode) {
    renderer.setDarkMode(true);
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  const std::string sleepDir = SleepImageUtils::resolveConfiguredSleepDirectory();
  auto dir = sleepDir.empty() ? FsFile{} : Storage.open(sleepDir.c_str());

  std::string selectedPath;
  bool selectedIsPng = false;

  if (dir && dir.isDirectory()) {
    std::vector<std::string> files;
    char name[500];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) {
        file.close();
        continue;
      }
      file.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        file.close();
        continue;
      }

      const bool isBmp = FsHelpers::hasBmpExtension(filename);
      const bool isPng = FsHelpers::hasPngExtension(filename);
      if (!isBmp && !isPng) {
        LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
        file.close();
        continue;
      }

      if (isBmp) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() != BmpReaderError::Ok) {
          LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
          file.close();
          continue;
        }
      }

      files.emplace_back(filename);
      file.close();
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      uint16_t fileIndex = 0;
      const uint16_t recentIndex = APP_STATE.getMostRecentSleepIndex();
      if (SETTINGS.sleepImageOrder == CrossPointSettings::SLEEP_IMAGE_SEQUENTIAL) {
        if (recentIndex == UINT16_MAX || recentIndex >= numFiles - 1) {
          fileIndex = 0;
        } else {
          fileIndex = static_cast<uint16_t>(recentIndex + 1);
        }
      } else {
        const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
        const uint8_t window =
            static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
        fileIndex = static_cast<uint16_t>(random(fileCount));
        for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(fileIndex, window); attempt++) {
          fileIndex = static_cast<uint16_t>(random(fileCount));
        }
      }

      APP_STATE.pushRecentSleep(fileIndex);
      APP_STATE.saveToFile();
      selectedPath = sleepDir + "/" + files[static_cast<size_t>(fileIndex)];
      selectedIsPng = FsHelpers::hasPngExtension(files[static_cast<size_t>(fileIndex)]);
    }
  }
  if (dir) {
    dir.close();
  }

  if (!selectedPath.empty()) {
    if (selectedIsPng) {
      if (renderPngSleepScreen(selectedPath)) {
        return;
      }
    } else {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
      FsFile file;
      if (SleepScreenCache::load(renderer, selectedPath)) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }
      if (Storage.openFileForRead("SLP", selectedPath, file)) {
        LOG_DBG("SLP", "Loading sleep image: %s", selectedPath.c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap, selectedPath);
          file.close();
          return;
        }
        file.close();
      }
    }
  }

  FsFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      if (SleepScreenCache::load(renderer, "/sleep.bmp")) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        file.close();
        return;
      }
      renderBitmapSleepScreen(bitmap, "/sleep.bmp");
      file.close();
      return;
    }
    file.close();
  }

  if (renderPngSleepScreen("/sleep.png")) {
    return;
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  constexpr int logoWidth = 174;
  constexpr int logoHeight = 24;
  constexpr int logoTextGap = 10;
  constexpr int subtitleGap = 25;
  const int logoX = (pageWidth - logoWidth) / 2;
  const int logoY = (pageHeight - logoHeight) / 2;
  const int titleY = logoY + logoHeight + logoTextGap;
  const int subtitleY = titleY + subtitleGap;

  renderer.clearScreen();
  renderer.drawIcon(Logo, logoX, logoY, logoWidth, logoHeight);
  renderer.drawCenteredText(UI_10_FONT_ID, titleY, tr(STR_CPR_VCODEX), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, subtitleY, tr(STR_SLEEPING));

  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0;
  float cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (!sourcePath.empty() && canUseSleepCache(bitmap)) {
    SleepScreenCache::save(renderer, sourcePath);
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderPngSleepScreen(const std::string& sourcePath) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (!PngSleepRenderer::drawTransparentPng(sourcePath, renderer, 0, 0, pageWidth, pageHeight)) {
    return false;
  }

  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return true;
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  const bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  FsFile file;
  if (SleepScreenCache::load(renderer, coverBmpPath)) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap, coverBmpPath);
      file.close();
      return;
    }
    file.close();
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
