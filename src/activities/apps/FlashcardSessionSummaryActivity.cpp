#include "FlashcardSessionSummaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int METRIC_CARD_HEIGHT = 72;
constexpr int METRIC_CARD_GAP = 8;

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  renderer.drawText(UI_12_FONT_ID, rect.x + 10, rect.y + 12, value.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + 10, rect.y + 46, label);
}
}  // namespace

void FlashcardSessionSummaryActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FlashcardSessionSummaryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void FlashcardSessionSummaryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardWidth = (pageWidth - sidePadding * 2 - METRIC_CARD_GAP) / 2;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), summary.deckTitle.c_str());

  renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop, tr(STR_SESSION_SUMMARY), true, EpdFontFamily::BOLD);

  int currentY = contentTop + 24;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_REVIEWED),
                 std::to_string(summary.reviewed));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_CORRECT), std::to_string(summary.correct));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_FAILED),
                 std::to_string(summary.failed));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SKIPPED), std::to_string(summary.skipped));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_NEW_SEEN),
                 std::to_string(summary.newSeen));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SUCCESS_RATE), std::to_string(summary.successRatePercent) + "%");
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP + 6;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_SEEN),
                 std::to_string(summary.seenCards) + "/" + std::to_string(summary.totalCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_DUE_LEFT), std::to_string(summary.dueRemaining));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_MASTERED),
                 std::to_string(summary.masteredCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SESSIONS), std::to_string(summary.sessionCount));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
