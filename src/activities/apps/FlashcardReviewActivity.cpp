#include "FlashcardReviewActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
struct WrappedCardBody {
  int fontId = UI_10_FONT_ID;
  int lineHeight = 0;
  std::vector<std::string> lines;
  bool fits = true;
};

bool hasValue(const std::vector<std::string>& values, const std::string& key) {
  return std::find(values.begin(), values.end(), key) != values.end();
}

void drawReviewHeaderHints(GfxRenderer& renderer, const ThemeMetrics& metrics, const int pageWidth) {
  const int headerY = metrics.topPadding + 5;
  const char* exitLabel = tr(STR_EXIT);
  const int exitWidth = renderer.getTextWidth(SMALL_FONT_ID, exitLabel);
  const int boxHeight = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int boxPaddingX = 6;
  const int boxWidth = exitWidth + boxPaddingX * 2;
  const int exitGap = 22;
  const int exit2X = pageWidth - 210 - boxWidth;
  const int exit1X = exit2X - exitGap - exitWidth;
  const int boxY = headerY - 2;
  const int exit1BoxX = exit2X - exitGap - boxWidth;

  renderer.drawRect(exit1BoxX, boxY, boxWidth, boxHeight);
  renderer.drawText(SMALL_FONT_ID, exit1BoxX + boxPaddingX, headerY, exitLabel);

  renderer.drawRect(exit2X, boxY, boxWidth, boxHeight);
  renderer.drawText(SMALL_FONT_ID, exit2X + boxPaddingX, headerY, exitLabel);
}

std::vector<std::string> wrapCardBody(GfxRenderer& renderer, const int fontId, const std::string& text, const int width,
                                      const int maxLines, const EpdFontFamily::Style style) {
  std::vector<std::string> result;
  if (text.empty() || maxLines <= 0) {
    return result;
  }

  size_t start = 0;
  while (start <= text.size() && static_cast<int>(result.size()) < maxLines) {
    const size_t end = text.find('\n', start);
    const std::string segment = end == std::string::npos ? text.substr(start) : text.substr(start, end - start);

    if (segment.empty()) {
      result.emplace_back("");
    } else {
      const int remainingLines = maxLines - static_cast<int>(result.size());
      auto wrapped = renderer.wrappedText(fontId, segment.c_str(), width, remainingLines, style);
      result.insert(result.end(), wrapped.begin(), wrapped.end());
    }

    if (end == std::string::npos) {
      break;
    }

    start = end + 1;
  }

  if (static_cast<int>(result.size()) > maxLines) {
    result.resize(maxLines);
  }

  return result;
}

int builtInReaderFontId(uint8_t family, uint8_t size) {
  switch (family) {
    case CrossPointSettings::NOTOSANS:
      switch (size) {
        case CrossPointSettings::X_SMALL:
          return NOTOSANS_10_FONT_ID;
        case CrossPointSettings::SMALL:
          return NOTOSANS_12_FONT_ID;
        case CrossPointSettings::LARGE:
          return NOTOSANS_16_FONT_ID;
        case CrossPointSettings::EXTRA_LARGE:
          return NOTOSANS_18_FONT_ID;
        case CrossPointSettings::MEDIUM:
        default:
          return NOTOSANS_14_FONT_ID;
      }
    case CrossPointSettings::LEXEND:
      switch (size) {
        case CrossPointSettings::X_SMALL:
          return LEXEND_10_FONT_ID;
        case CrossPointSettings::SMALL:
          return LEXEND_12_FONT_ID;
        case CrossPointSettings::LARGE:
          return LEXEND_16_FONT_ID;
        case CrossPointSettings::EXTRA_LARGE:
          return LEXEND_18_FONT_ID;
        case CrossPointSettings::MEDIUM:
        default:
          return LEXEND_14_FONT_ID;
      }
    case CrossPointSettings::BOOKERLY:
    default:
      switch (size) {
        case CrossPointSettings::X_SMALL:
          return BOOKERLY_10_FONT_ID;
        case CrossPointSettings::SMALL:
          return BOOKERLY_12_FONT_ID;
        case CrossPointSettings::LARGE:
          return BOOKERLY_16_FONT_ID;
        case CrossPointSettings::EXTRA_LARGE:
          return BOOKERLY_18_FONT_ID;
        case CrossPointSettings::MEDIUM:
        default:
          return BOOKERLY_14_FONT_ID;
      }
  }
}

std::vector<int> flashcardFontCandidates(int preferredFontId) {
  std::vector<int> candidates;
  const auto addUnique = [&candidates](const int fontId) {
    if (fontId != 0 && std::find(candidates.begin(), candidates.end(), fontId) == candidates.end()) {
      candidates.push_back(fontId);
    }
  };

  addUnique(preferredFontId);
  const int startSize = std::clamp<int>(SETTINGS.fontSize, CrossPointSettings::X_SMALL, CrossPointSettings::EXTRA_LARGE);
  for (int size = startSize; size >= CrossPointSettings::X_SMALL; --size) {
    addUnique(builtInReaderFontId(SETTINGS.fontFamily, static_cast<uint8_t>(size)));
  }
  addUnique(UI_12_FONT_ID);
  addUnique(UI_10_FONT_ID);
  addUnique(SMALL_FONT_ID);
  return candidates;
}

WrappedCardBody fitCardBody(GfxRenderer& renderer, const int preferredFontId, const std::string& text, const int width,
                            const int height, const EpdFontFamily::Style style) {
  WrappedCardBody fallback;

  for (const int fontId : flashcardFontCandidates(preferredFontId)) {
    const int lineHeight = std::max(1, renderer.getLineHeight(fontId));
    const int maxLines = std::max(1, height / lineHeight);
    auto lines = wrapCardBody(renderer, fontId, text, width, maxLines + 1, style);
    const bool fits = static_cast<int>(lines.size()) <= maxLines;
    if (!fits && static_cast<int>(lines.size()) > maxLines) {
      lines.resize(maxLines);
    }

    fallback = WrappedCardBody{fontId, lineHeight, std::move(lines), fits};
    if (fits) {
      return fallback;
    }
  }

  return fallback;
}
}  // namespace

void FlashcardReviewActivity::loadDeckData() {
  errorMessage.clear();
  if (!FLASHCARDS.loadDeck(deckPath, deck, &errorMessage)) {
    loaded = false;
    return;
  }

  FLASHCARDS.loadDeckProgress(deck, progress);
  const FlashcardDeckMetrics metrics = FLASHCARDS.buildMetrics(deck, progress);
  FLASHCARDS.registerDeckOpened(deck, metrics);
  queue = FLASHCARDS.buildSessionQueue(deck, progress);
  initialSessionSize = static_cast<int>(queue.size());
  loaded = true;
}

bool FlashcardReviewActivity::isCurrentCardUnseen() const {
  if (queueIndex >= queue.size()) {
    return false;
  }
  const int index = queue[queueIndex];
  return index >= 0 && index < static_cast<int>(progress.size()) && progress[index].seenCount == 0;
}

FlashcardCardProgress& FlashcardReviewActivity::currentProgress() { return progress[queue[queueIndex]]; }

const FlashcardCard& FlashcardReviewActivity::currentCard() const { return deck.cards[queue[queueIndex]]; }

void FlashcardReviewActivity::goToNextCard() {
  showBack = false;
  const int sessionHandled = sessionReviewed + sessionSkipped;
  if (SETTINGS.flashcardStudyMode != CrossPointSettings::FLASHCARD_STUDY_INFINITE && initialSessionSize > 0 &&
      sessionHandled >= initialSessionSize) {
    finishWithSummary();
    return;
  }
  queueIndex++;
  if (queueIndex >= queue.size()) {
    if (SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_INFINITE) {
      queue = FLASHCARDS.buildSessionQueue(deck, progress);
      queueIndex = 0;
      if (queue.empty()) {
        finishWithSummary();
        return;
      }
      requestUpdate();
      return;
    }
    finishWithSummary();
    return;
  }
  requestUpdate();
}

void FlashcardReviewActivity::markCurrentSuccess() {
  auto& item = currentProgress();
  if (isCurrentCardUnseen() && !hasValue(newlySeenKeys, item.key)) {
    newlySeenKeys.push_back(item.key);
    sessionNewSeen++;
  }
  FLASHCARDS.markCardSuccess(item);
  sessionReviewed++;
  sessionCorrect++;
  goToNextCard();
}

void FlashcardReviewActivity::markCurrentFailure() {
  const int cardIndex = queue[queueIndex];
  auto& item = currentProgress();
  if (isCurrentCardUnseen() && !hasValue(newlySeenKeys, item.key)) {
    newlySeenKeys.push_back(item.key);
    sessionNewSeen++;
  }
  FLASHCARDS.markCardFailure(item);
  queue.push_back(cardIndex);
  sessionReviewed++;
  sessionFailed++;
  goToNextCard();
}

void FlashcardReviewActivity::skipCurrentCard() {
  const int cardIndex = queue[queueIndex];
  auto& item = currentProgress();
  if (isCurrentCardUnseen() && !hasValue(newlySeenKeys, item.key)) {
    newlySeenKeys.push_back(item.key);
    sessionNewSeen++;
  }
  FLASHCARDS.markCardSkipped(item);
  queue.push_back(cardIndex);
  sessionSkipped++;
  goToNextCard();
}

void FlashcardReviewActivity::finishWithSummary() {
  if (loaded) {
    FLASHCARDS.saveDeckProgress(deck, progress);
    const FlashcardDeckMetrics metrics = FLASHCARDS.buildMetrics(deck, progress);
    FLASHCARDS.registerSession(deck, metrics);

    FlashcardSessionResult result;
    result.deckId = deck.deckId;
    result.deckPath = deck.path;
    result.deckTitle = deck.title;
    result.reviewed = sessionReviewed;
    result.correct = sessionCorrect;
    result.failed = sessionFailed;
    result.skipped = sessionSkipped;
    result.newSeen = sessionNewSeen;
    result.totalCards = metrics.totalCards;
    result.seenCards = metrics.seenCards;
    result.unseenCards = metrics.unseenCards;
    result.dueCards = metrics.dueCards;
    result.masteredCards = metrics.masteredCards;
    result.successRatePercent = metrics.successRatePercent;
    result.sessionCount = metrics.sessionCount + 1;
    result.dueRemaining = metrics.dueCards;
    setResult(ActivityResult{result});
  } else {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
  }
  finish();
}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  renderer.requestNextFullRefresh();
  orientationApplied = true;
  loadDeckData();
  requestUpdate(true);
}

void FlashcardReviewActivity::onExit() {
  if (orientationApplied) {
    renderer.setOrientation(originalOrientation);
    renderer.requestNextFullRefresh();
  }
  Activity::onExit();
}

void FlashcardReviewActivity::loop() {
  if (!loaded) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }

  if (queue.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finishWithSummary();
    }
    return;
  }

  const bool backReleased = mappedInput.wasReleased(MappedInputManager::Button::Back);
  const bool pageBackReleased = mappedInput.wasReleased(MappedInputManager::Button::PageBack);
  const bool pageForwardReleased = mappedInput.wasReleased(MappedInputManager::Button::PageForward);

  if (backReleased) {
    showBack = !showBack;
    requestUpdate();
    return;
  }

  if (pageBackReleased || pageForwardReleased) {
    finishWithSummary();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    skipCurrentCard();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    markCurrentSuccess();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    markCurrentFailure();
    return;
  }
}

void FlashcardReviewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (!loaded) {
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_OPEN));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10,
                              errorMessage.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : errorMessage.c_str());
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, tr(STR_BACK));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  if (queue.empty()) {
    HeaderDateUtils::drawHeaderWithDate(renderer, deck.title.c_str(), tr(STR_NO_DUE_CARDS));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_NO_DUE_CARDS));
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, tr(STR_BACK));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }

  const bool showSessionProgress = SETTINGS.flashcardStudyMode != CrossPointSettings::FLASHCARD_STUDY_INFINITE;
  const int sessionHandled = sessionReviewed + sessionSkipped;
  const int completedCards = std::clamp(sessionHandled, 0, initialSessionSize);
  const std::string headerSubtitle =
      showSessionProgress && initialSessionSize > 0
          ? std::to_string(completedCards) + "/" + std::to_string(initialSessionSize)
          : std::string();
  const std::string headerTitle =
      showSessionProgress && !headerSubtitle.empty() ? deck.title + " / " + headerSubtitle : deck.title;
  HeaderDateUtils::drawHeaderWithDate(renderer, headerTitle.c_str(), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int sideHintsReserve = metrics.sideButtonHintsWidth + metrics.verticalSpacing + 8;
  const int cardX = metrics.contentSidePadding;
  const int cardY = contentTop;
  const int cardWidth = pageWidth - metrics.contentSidePadding * 2 - sideHintsReserve;
  const int cardHeight = contentBottom - contentTop;
  const int readerFontId = SETTINGS.getReaderFontId();

  renderer.drawRect(cardX, cardY, cardWidth, cardHeight);

  const std::string sideLabel = showBack ? tr(STR_CARD_BACK) : tr(STR_CARD_FRONT);
  renderer.drawText(SMALL_FONT_ID, cardX + 10, cardY + 10, sideLabel.c_str(), true, EpdFontFamily::BOLD);

  const std::string bodyText = showBack ? currentCard().back : currentCard().front;
  const int textWidth = cardWidth - 24;
  const int textTop = cardY + 34;
  const int textHeight = cardHeight - 50;
  const EpdFontFamily::Style bodyStyle = showBack ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const auto fitted = fitCardBody(renderer, readerFontId, bodyText, textWidth, textHeight, bodyStyle);
  int textY = textTop + std::max(0, (textHeight - static_cast<int>(fitted.lines.size()) * fitted.lineHeight) / 2);
  for (const auto& line : fitted.lines) {
    renderer.drawText(fitted.fontId, cardX + 12, textY, line.c_str(), true, bodyStyle);
    textY += fitted.lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_FLIP), tr(STR_NEXT), tr(STR_SUCCESS), tr(STR_FAIL));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  drawReviewHeaderHints(renderer, metrics, pageWidth);

  renderer.displayBuffer();
}
