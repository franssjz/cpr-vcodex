#include "FlashcardStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FlashcardDeckStatsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long DELETE_FLASHCARD_STATS_HOLD_MS = 1000;

bool hasStatsToShow(const FlashcardDeckRecord& record) {
  return record.sessionCount > 0 || record.seenCards > 0 || record.totalReviewed > 0 || record.totalCorrect > 0 ||
         record.totalWrong > 0 || record.totalSkipped > 0 || record.lastReviewedAt > 0;
}

std::string buildDeckSubtitle(const FlashcardDeckRecord& record) {
  const int answered = static_cast<int>(record.totalCorrect + record.totalWrong);
  const int accuracy = answered > 0 ? static_cast<int>((record.totalCorrect * 100) / answered) : 0;
  return std::to_string(record.seenCards) + "/" + std::to_string(record.totalCards) + " | " + std::to_string(accuracy) +
         "%";
}
}  // namespace

void FlashcardStatsActivity::reloadDecks() {
  decks.clear();
  for (const auto& record : FLASHCARDS.getKnownDecks()) {
    if (hasStatsToShow(record)) {
      decks.push_back(record);
    }
  }
  std::sort(decks.begin(), decks.end(), [](const FlashcardDeckRecord& lhs, const FlashcardDeckRecord& rhs) {
    if (lhs.lastOpenedAt != rhs.lastOpenedAt) {
      return lhs.lastOpenedAt > rhs.lastOpenedAt;
    }
    return lhs.title < rhs.title;
  });

  if (decks.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(decks.size()) - 1);
  }
}

void FlashcardStatsActivity::onEnter() {
  Activity::onEnter();
  reloadDecks();
  requestUpdate();
}

void FlashcardStatsActivity::loop() {
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(decks.size())) {
      if (mappedInput.getHeldTime() >= DELETE_FLASHCARD_STATS_HOLD_MS) {
        const auto selectedDeck = decks[selectedIndex];
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_STATS_ENTRY), selectedDeck.title),
            [this, deckId = selectedDeck.deckId](const ActivityResult& result) {
              if (!result.isCancelled) {
                FLASHCARDS.resetDeckStats(deckId);
              }
              reloadDecks();
              requestUpdate();
            });
        return;
      }

      startActivityForResult(std::make_unique<FlashcardDeckStatsActivity>(renderer, mappedInput, decks[selectedIndex].path),
                             [this](const ActivityResult&) {
                               reloadDecks();
                               requestUpdate();
                             });
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(decks.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(decks.size()));
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, static_cast<int>(decks.size()), pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, static_cast<int>(decks.size()), pageItems);
    requestUpdate();
  });
}

void FlashcardStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_STATISTICS));

  if (decks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(decks.size()), selectedIndex,
                 [this](const int index) { return decks[index].title; },
                 [this](const int index) { return buildDeckSubtitle(decks[index]); },
                 [](const int) { return UIIcon::Library; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), decks.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
