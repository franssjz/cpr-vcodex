#include "FlashcardBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "FlashcardReviewActivity.h"
#include "FlashcardSessionSummaryActivity.h"
#include "FlashcardsStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long GO_ROOT_MS = 1000;

bool hasCsvExtension(const std::string_view filename) {
  if (filename.size() < 4) {
    return false;
  }
  const size_t dotPos = filename.rfind('.');
  if (dotPos == std::string_view::npos) {
    return false;
  }
  std::string extension(filename.substr(dotPos));
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".csv";
}

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    const bool isDir1 = !str1.empty() && str1.back() == '/';
    const bool isDir2 = !str2.empty() && str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    const char* s1 = str1.c_str();
    const char* s2 = str2.c_str();
    while (*s1 && *s2) {
      if (isdigit(*s1) && isdigit(*s2)) {
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;
        const char* num1 = s1;
        const char* num2 = s2;
        while (isdigit(*s1)) s1++;
        while (isdigit(*s2)) s2++;
        const int len1 = s1 - num1;
        const int len2 = s2 - num2;
        if (len1 != len2) return len1 < len2;
        const int cmp = strncmp(num1, num2, len1);
        if (cmp != 0) return cmp < 0;
        const int leading1 = num1 - start1;
        const int leading2 = num2 - start2;
        if (leading1 != leading2) return leading1 < leading2;
      } else {
        const char c1 = static_cast<char>(tolower(*s1));
        const char c2 = static_cast<char>(tolower(*s2));
        if (c1 != c2) return c1 < c2;
        ++s1;
        ++s2;
      }
    }
    return *s1 < *s2;
  });
}

std::string getFileName(std::string filename) {
  if (!filename.empty() && filename.back() == '/') {
    filename.pop_back();
    return filename;
  }
  return filename;
}
}  // namespace

void FlashcardBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      std::string_view filename{name};
      if (hasCsvExtension(filename)) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();
  sortFileList(files);
}

bool FlashcardBrowserActivity::openDeckPath(const std::string& path) {
  FlashcardDeck deck;
  std::string error;
  if (!FLASHCARDS.loadDeck(path, deck, &error)) {
    transientMessage = error.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : error;
    transientUntilMs = millis() + 1500;
    requestUpdate(true);
    return false;
  }

  startActivityForResult(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, path), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }

    if (const auto* session = std::get_if<FlashcardSessionResult>(&result.data)) {
      startActivityForResult(std::make_unique<FlashcardSessionSummaryActivity>(renderer, mappedInput, *session),
                             [this](const ActivityResult&) { requestUpdate(); });
      return;
    }
    requestUpdate();
  });
  return true;
}

void FlashcardBrowserActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    root.close();
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    root.close();
    loadFiles();
  }

  requestUpdate();
}

void FlashcardBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void FlashcardBrowserActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_ROOT_MS &&
      basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) {
      return;
    }

    const std::string& entry = files[selectorIndex];
    const bool isDirectory = !entry.empty() && entry.back() == '/';
    if (isDirectory) {
      if (basepath.back() != '/') {
        basepath += "/";
      }
      basepath += entry.substr(0, entry.length() - 1);
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
      return;
    }

    std::string fullPath = basepath;
    if (fullPath.back() != '/') {
      fullPath += "/";
    }
    fullPath += entry;
    (void)openDeckPath(fullPath);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < GO_ROOT_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;
        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);
        requestUpdate();
      } else {
        finish();
      }
    }
  }

  const int listSize = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void FlashcardBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_OPEN));

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;

  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(files.size()),
                 static_cast<int>(selectorIndex), [this](const int index) { return getFileName(files[index]); }, nullptr,
                 [this](const int index) { return UITheme::getFileIcon(files[index]); });
  }

  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int separatorY = pathY - metrics.verticalSpacing / 2;
  renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
  const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
  const char* pathStr = basepath.c_str();
  const char* pathDisplay = pathStr;
  char leftTruncBuf[256];
  if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
    const char ellipsis[] = "\xe2\x80\xa6";
    const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
    const int available = pathMaxWidth - ellipsisWidth;
    const char* p = pathStr;
    while (*p) {
      if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
      ++p;
      while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
    }
    snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
    pathDisplay = leftTruncBuf;
  }
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);

  if (!transientMessage.empty() && millis() < transientUntilMs) {
    renderer.drawCenteredText(SMALL_FONT_ID, contentTop + contentHeight - 18, transientMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(basepath == "/" ? tr(STR_BACK) : tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                                            files.empty() ? "" : tr(STR_DIR_UP), files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

size_t FlashcardBrowserActivity::findEntry(const std::string& name) const {
  for (size_t index = 0; index < files.size(); ++index) {
    if (files[index] == name) {
      return index;
    }
  }
  return 0;
}
