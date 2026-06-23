#include "BookMetadataActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

BookMetadataActivity::BookMetadataActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& bookPath)
    : Activity("BookMetadata", renderer, mappedInput), bookPath(bookPath) {}

void BookMetadataActivity::loadMetadata() {
  lines.clear();
  descriptionLines.clear();

  if (!FsHelpers::hasEpubExtension(bookPath)) {
    lines.push_back({"", tr(STR_VIEW_METADATA)});
    return;
  }

  Epub epub(bookPath, "/.crosspoint");
  if (!epub.load(true, true)) {
    LOG_ERR("BKM", "Failed to load EPUB: %s", bookPath.c_str());
    lines.push_back({"", "Failed to load book metadata"});
    return;
  }

  const auto& title = epub.getTitle();
  const auto& author = epub.getAuthor();
  const auto& publisher = epub.getPublisher();
  const auto& description = epub.getDescription();
  const auto& date = epub.getPublicationDate();
  const auto& identifier = epub.getIdentifier();
  const auto& subject = epub.getSubject();
  const auto& language = epub.getLanguage();

  auto addLine = [this](const char* label, const std::string& value) {
    if (!value.empty()) {
      lines.push_back({label, value});
    }
  };

  addLine(tr(STR_METADATA_TITLE), title);
  addLine(tr(STR_METADATA_AUTHOR), author);
  addLine(tr(STR_METADATA_PUBLISHER), publisher);
  addLine(tr(STR_METADATA_DATE), date);
  addLine(tr(STR_METADATA_IDENTIFIER), identifier);
  addLine(tr(STR_METADATA_SUBJECT), subject);
  addLine(tr(STR_METADATA_LANGUAGE), language);

  if (!description.empty()) {
    static constexpr size_t MAX_DESC_LEN = 1200;
    const std::string clippedDesc = description.size() > MAX_DESC_LEN
                                        ? description.substr(0, MAX_DESC_LEN) + "..."
                                        : description;
    const int maxW = renderer.getScreenWidth() - 40;
    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, clippedDesc.c_str(), maxW, 8);
    for (const auto& w : wrapped) {
      descriptionLines.push_back(w);
    }
  }
}

void BookMetadataActivity::onEnter() {
  Activity::onEnter();
  loadMetadata();
  scrollOffset = 0;
  requestUpdate(true);
}

void BookMetadataActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    scrollOffset += 12;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    scrollOffset -= 12;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }
}

void BookMetadataActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Header style like other frontend pages
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_VIEW_METADATA));

  int currentY = contentTop;

  // Combine metadata lines into render queue
  std::vector<std::string> renderQueue;
  for (const auto& line : lines) {
    if (!line.label.empty()) {
      renderQueue.push_back("__label__" + line.label);
      const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, line.value.c_str(), pageWidth - sidePadding * 2 - 90, 3);
      for (const auto& w : wrapped) {
        renderQueue.push_back("__value__" + w);
      }
      renderQueue.push_back("__gap__");
    } else {
      renderQueue.push_back("__plain__" + line.value);
    }
  }
  if (!descriptionLines.empty()) {
    renderQueue.push_back("__desc_header__");
    for (const auto& dline : descriptionLines) {
      renderQueue.push_back("__desc__" + dline);
    }
  }

  const int maxY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 10;
  const int availableViewport = maxY - currentY;
  const int totalHeight = static_cast<int>(renderQueue.size()) * lineHeight;
  const int maxScroll = std::max(0, totalHeight - availableViewport);
  scrollOffset = std::clamp(scrollOffset, 0, maxScroll);

  const int startLine = std::max(0, (scrollOffset * lineHeight) / lineHeight);
  const int visibleCount = std::min(static_cast<int>(renderQueue.size()), startLine + availableViewport / lineHeight);

  int drawY = currentY - (startLine * lineHeight);
  bool firstDesc = false;
  for (int i = startLine; i < visibleCount; ++i) {
    const auto& item = renderQueue[i];
    if (item.rfind("__label__", 0) == 0) {
      const std::string label = item.substr(9);
      renderer.drawText(UI_10_FONT_ID, sidePadding, drawY, label.c_str(), true, EpdFontFamily::BOLD);
      drawY += lineHeight;
    } else if (item.rfind("__value__", 0) == 0) {
      const std::string value = item.substr(9);
      renderer.drawText(UI_10_FONT_ID, sidePadding + 90, drawY, value.c_str(), true, EpdFontFamily::REGULAR);
      drawY += lineHeight;
    } else if (item.rfind("__desc_header__", 0) == 0) {
      renderer.drawText(UI_10_FONT_ID, sidePadding, drawY, tr(STR_METADATA_DESCRIPTION), true, EpdFontFamily::BOLD);
      drawY += lineHeight;
      firstDesc = false;
    } else if (item.rfind("__desc__", 0) == 0) {
      const std::string dline = item.substr(8);
      renderer.drawText(UI_10_FONT_ID, sidePadding + 4, drawY, dline.c_str(), true, EpdFontFamily::REGULAR);
      drawY += lineHeight;
    } else if (item.rfind("__gap__", 0) == 0) {
      drawY += 6;
    } else {
      renderer.drawText(UI_10_FONT_ID, sidePadding, drawY, item.c_str(), true, EpdFontFamily::REGULAR);
      drawY += lineHeight;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
