#include "SleepPreviewActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/SleepImageUtils.h"
#include "util/PngSleepRenderer.h"

namespace {
void drawPreviewBitmap(GfxRenderer& renderer, const Rect& contentRect, Bitmap& bitmap) {
  int x;
  int y;

  if (bitmap.getWidth() > contentRect.width || bitmap.getHeight() > contentRect.height) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float rectRatio = static_cast<float>(contentRect.width) / static_cast<float>(contentRect.height);

    if (ratio > rectRatio) {
      x = contentRect.x;
      y = contentRect.y +
          std::round((static_cast<float>(contentRect.height) - static_cast<float>(contentRect.width) / ratio) / 2.0f);
    } else {
      x = contentRect.x +
          std::round((static_cast<float>(contentRect.width) - static_cast<float>(contentRect.height) * ratio) / 2.0f);
      y = contentRect.y;
    }

    renderer.drawBitmap(bitmap, x, y, contentRect.width, contentRect.height, 0, 0);
    return;
  }

  x = contentRect.x + (contentRect.width - bitmap.getWidth()) / 2;
  y = contentRect.y + (contentRect.height - bitmap.getHeight()) / 2;
  renderer.drawBitmap(bitmap, x, y, bitmap.getWidth(), bitmap.getHeight(), 0, 0);
}

bool drawPreviewPng(GfxRenderer& renderer, const Rect& contentRect, const std::string& imagePath) {
  return PngSleepRenderer::drawTransparentPng(imagePath, renderer, contentRect.x, contentRect.y, contentRect.width,
                                              contentRect.height);
}

void drawPreviewFrame(GfxRenderer& renderer, const std::string& directoryLabel, const std::string& subtitle,
                      const char* btn1, const char* btn2, const char* btn3, const char* btn4) {
  renderer.clearScreen();
  HeaderDateUtils::drawHeaderWithDate(renderer, directoryLabel.c_str(), subtitle.empty() ? nullptr : subtitle.c_str());
  GUI.drawButtonHints(renderer, btn1, btn2, btn3, btn4);
}
}  // namespace

void SleepPreviewActivity::onEnter() {
  Activity::onEnter();
  loading = true;
  selectedIndex = 0;
  previewDirty = true;
  requestUpdate();
}

void SleepPreviewActivity::loadImages() {
  imagePaths = SleepImageUtils::listImageFiles(directoryPath);
  if (selectedIndex >= static_cast<int>(imagePaths.size())) {
    selectedIndex = imagePaths.empty() ? 0 : static_cast<int>(imagePaths.size()) - 1;
  }
  previewDirty = !imagePaths.empty();
}

void SleepPreviewActivity::loop() {
  if (loading) {
    loadImages();
    loading = false;
    requestUpdate(true);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    strncpy(SETTINGS.sleepDirectory, directoryPath.c_str(), sizeof(SETTINGS.sleepDirectory) - 1);
    SETTINGS.sleepDirectory[sizeof(SETTINGS.sleepDirectory) - 1] = '\0';
    SETTINGS.saveToFile();
    requestUpdate(true);
    return;
  }

  const int itemCount = static_cast<int>(imagePaths.size());
  if (itemCount == 0) {
    return;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    previewDirty = true;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    previewDirty = true;
    requestUpdate();
  });
}

void SleepPreviewActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const bool isSelectedDirectory = directoryPath == std::string(SETTINGS.sleepDirectory);
  const std::string directoryLabel = SleepImageUtils::getDirectoryLabel(directoryPath);
  const std::string subtitle =
      imagePaths.empty() ? (isSelectedDirectory ? std::string(tr(STR_SELECTED)) : std::string())
                         : (std::to_string(selectedIndex + 1) + "/" + std::to_string(imagePaths.size()));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_USE_DIRECTORY),
                                            imagePaths.empty() ? "" : tr(STR_DIR_UP),
                                            imagePaths.empty() ? "" : tr(STR_DIR_DOWN));

  drawPreviewFrame(renderer, directoryLabel, subtitle, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const Rect contentRect{metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
                         pageWidth - metrics.contentSidePadding * 2,
                         pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight +
                                       metrics.verticalSpacing * 3)};

  if (loading) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 10, tr(STR_LOADING), true, EpdFontFamily::BOLD);
  } else if (imagePaths.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_NO_FILES_FOUND));
  } else {
    Rect popupRect{};
    if (previewDirty) {
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 20);
    }

    FsFile file;
    if (Storage.openFileForRead("SLP", imagePaths[selectedIndex], file)) {
      if (previewDirty) {
        GUI.fillPopupProgress(renderer, popupRect, 55);
      }
      const bool isPng = FsHelpers::hasPngExtension(imagePaths[selectedIndex]);
      bool rendered = false;

      if (isPng) {
        if (previewDirty) {
          GUI.fillPopupProgress(renderer, popupRect, 90);
          drawPreviewFrame(renderer, directoryLabel, subtitle, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        }
        rendered = drawPreviewPng(renderer, contentRect, imagePaths[selectedIndex]);
      } else {
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          if (previewDirty) {
            GUI.fillPopupProgress(renderer, popupRect, 90);
            drawPreviewFrame(renderer, directoryLabel, subtitle, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          }
          drawPreviewBitmap(renderer, contentRect, bitmap);
          rendered = true;
        }
      }

      if (!rendered) {
        if (previewDirty) {
          drawPreviewFrame(renderer, directoryLabel, subtitle, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        }
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, "Invalid image file");
      }
      file.close();
    } else {
      if (previewDirty) {
        drawPreviewFrame(renderer, directoryLabel, subtitle, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, "Could not open file");
    }

    previewDirty = false;
  }

  renderer.displayBuffer();
}
