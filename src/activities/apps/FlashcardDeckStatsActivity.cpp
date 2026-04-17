#include "FlashcardDeckStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FlashcardReviewActivity.h"
#include "FlashcardSessionSummaryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int METRIC_CARD_HEIGHT = 74;
constexpr int METRIC_CARD_GAP = 8;

std::string formatDateOrFallback(const uint32_t timestamp) {
  const std::string date = TimeUtils::formatDate(timestamp);
  return date.empty() ? std::string(tr(STR_NOT_SET)) : date;
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  renderer.drawText(UI_12_FONT_ID, rect.x + 10, rect.y + 12, value.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + 10, rect.y + 48, label);
}
}  // namespace

void FlashcardDeckStatsActivity::loadDeckData() {
  errorMessage.clear();
  if (!FLASHCARDS.loadDeck(deckPath, deck, &errorMessage)) {
    loaded = false;
    return;
  }
  FLASHCARDS.loadDeckProgress(deck, progress);
  metrics = FLASHCARDS.buildMetrics(deck, progress);
  loaded = true;
}

void FlashcardDeckStatsActivity::onEnter() {
  Activity::onEnter();
  loadDeckData();
  requestUpdate();
}

void FlashcardDeckStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (loaded && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    startActivityForResult(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, deckPath),
                           [this](const ActivityResult& result) {
                             loadDeckData();
                             if (const auto* session = std::get_if<FlashcardSessionResult>(&result.data)) {
                               startActivityForResult(std::make_unique<FlashcardSessionSummaryActivity>(renderer, mappedInput, *session),
                                                      [this](const ActivityResult&) {
                                                        loadDeckData();
                                                        requestUpdate();
                                                      });
                               return;
                             }
                             requestUpdate();
                           });
  }
}

void FlashcardDeckStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metricsUi = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metricsUi.contentSidePadding;
  const int contentTop = metricsUi.topPadding + metricsUi.headerHeight + metricsUi.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), loaded ? deck.title.c_str() : tr(STR_STATISTICS));

  if (!loaded) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24,
                              errorMessage.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int cardWidth = (pageWidth - sidePadding * 2 - METRIC_CARD_GAP) / 2;
  int currentY = contentTop;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_TOTAL_CARDS),
                 std::to_string(metrics.totalCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SEEN), std::to_string(metrics.seenCards));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_UNSEEN),
                 std::to_string(metrics.unseenCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_DUE), std::to_string(metrics.dueCards));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_MASTERED),
                 std::to_string(metrics.masteredCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SUCCESS_RATE), std::to_string(metrics.successRatePercent) + "%");
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_SESSIONS),
                 std::to_string(metrics.sessionCount));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_LAST_REVIEW), formatDateOrFallback(metrics.lastReviewedAt));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
