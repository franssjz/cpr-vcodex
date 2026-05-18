#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/themes/lyra/LyraCustomTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/lyra/LyraVcodex2Theme.h"

namespace {
constexpr int SKIP_PAGE_MS = 700;
constexpr char kWidthPlaceholder[] = "[WIDTH]";
constexpr char kHeightPlaceholder[] = "[HEIGHT]";
constexpr size_t kWidthPlaceholderLength = sizeof(kWidthPlaceholder) - 1;
constexpr size_t kHeightPlaceholderLength = sizeof(kHeightPlaceholder) - 1;
}  // namespace

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  switch (type) {
    case CrossPointSettings::UI_THEME::LYRA:
      LOG_DBG("UI", "Using Lyra theme");
      currentTheme = std::make_unique<LyraTheme>();
      currentMetrics = &LyraMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_VCODEX2:
      LOG_DBG("UI", "Using LyraVcodex2 theme");
      currentTheme = std::make_unique<LyraVcodex2Theme>();
      currentMetrics = &LyraVcodex2Metrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_CUSTOM:
      LOG_DBG("UI", "Using Lyra vCodex theme");
      currentTheme = std::make_unique<LyraCustomTheme>();
      currentMetrics = &LyraCustomMetrics::values;
      break;
    default:
      LOG_DBG("UI", "Using LyraVcodex2 fallback theme");
      currentTheme = std::make_unique<LyraVcodex2Theme>();
      currentMetrics = &LyraVcodex2Metrics::values;
      break;
  }
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight = renderer.getScreenHeight() - reservedHeight - extraReservedHeight;
  int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  if (coverHeight <= 0) {
    return "";
  }
  const int coverWidth = static_cast<int>((static_cast<int64_t>(coverHeight) * 3 + 2) / 5);
  return getCoverThumbPath(std::move(coverBmpPath), coverWidth, coverHeight);
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int width, int height) {
  if (width <= 0 || height <= 0) {
    return "";
  }

  const size_t widthPos = coverBmpPath.find(kWidthPlaceholder, 0);
  const size_t heightPos = coverBmpPath.find(kHeightPlaceholder, 0);
  const bool hasWidthPlaceholder = widthPos != std::string::npos;
  const bool hasHeightPlaceholder = heightPos != std::string::npos;

  if (!hasWidthPlaceholder && !hasHeightPlaceholder) {
    return coverBmpPath;
  }
  if ((hasWidthPlaceholder &&
       coverBmpPath.find(kWidthPlaceholder, widthPos + kWidthPlaceholderLength) != std::string::npos) ||
      (hasHeightPlaceholder &&
       coverBmpPath.find(kHeightPlaceholder, heightPos + kHeightPlaceholderLength) != std::string::npos)) {
    return "";
  }
  if (!hasHeightPlaceholder) {
    return "";
  }

  if (hasWidthPlaceholder) {
    coverBmpPath.replace(widthPos, kWidthPlaceholderLength, std::to_string(width));
  }

  const size_t updatedHeightPos = coverBmpPath.find(kHeightPlaceholder, 0);
  if (updatedHeightPos != std::string::npos) {
    if (hasWidthPlaceholder) {
      coverBmpPath.replace(updatedHeightPos, kHeightPlaceholderLength, std::to_string(height));
    } else {
      std::string legacyPath = coverBmpPath;
      legacyPath.replace(updatedHeightPos, kHeightPlaceholderLength, std::to_string(height));
      coverBmpPath.replace(updatedHeightPos, kHeightPlaceholderLength,
                           std::to_string(width) + "x" + std::to_string(height));
      if (!Storage.exists(coverBmpPath.c_str()) && Storage.exists(legacyPath.c_str())) {
        return legacyPath;
      }
    }
  }
  return coverBmpPath;
}

std::string UITheme::resolveCoverThumbPath(const std::string& coverBmpPath, const int preferredWidth,
                                           const int preferredHeight) {
  if (coverBmpPath.empty()) {
    return "";
  }

  if (preferredWidth > 0 && preferredHeight > 0) {
    const std::string preferred = getCoverThumbPath(coverBmpPath, preferredWidth, preferredHeight);
    if (!preferred.empty() && Storage.exists(preferred.c_str())) {
      return preferred;
    }
  }

  if (preferredHeight > 0) {
    const std::string preferred = getCoverThumbPath(coverBmpPath, preferredHeight);
    if (!preferred.empty() && Storage.exists(preferred.c_str())) {
      return preferred;
    }
  }

  const int candidateHeights[] = {400, 240, 180, 166, 164, 160, 154, 140, 132};
  for (const int height : candidateHeights) {
    const std::string candidate = getCoverThumbPath(coverBmpPath, height);
    if (!candidate.empty() && Storage.exists(candidate.c_str())) {
      return candidate;
    }
  }

  return Storage.exists(coverBmpPath.c_str()) ? coverBmpPath : "";
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getStatusBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  if (SETTINGS.statusBarPlacement == CrossPointSettings::STATUS_BAR_HIDDEN) {
    return 0;
  }

  // Add status bar margin
  const bool showStatusBar = SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage ||
                             SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE ||
                             SETTINGS.statusBarTimeLeft != CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_HIDE ||
                             SETTINGS.statusBarBattery;
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  return (showStatusBar ? (metrics.statusBarVerticalMargin) : 0) +
         (showProgressBar ? (((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop) : 0);
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  if (SETTINGS.statusBarPlacement == CrossPointSettings::STATUS_BAR_HIDDEN) {
    return 0;
  }
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  return (showProgressBar ? (((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop) : 0);
}
