#include "FlashcardRecentsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FlashcardReviewActivity.h"
#include "FlashcardSessionSummaryActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr unsigned long DELETE_RECENT_FLASHCARD_HOLD_MS = 1000;

std::string buildDeckSubtitle(const FlashcardDeckRecord& record) {
  const std::string progress = std::to_string(record.seenCards) + "/" + std::to_string(record.totalCards);
  const int answered = static_cast<int>(record.totalCorrect + record.totalWrong);
  const int accuracy = answered > 0 ? static_cast<int>((record.totalCorrect * 100) / answered) : 0;
  return progress + " | " + std::to_string(accuracy) + "%";
}
}  // namespace

void FlashcardRecentsActivity::reloadDecks() {
  decks = FLASHCARDS.getRecentDecks();
  if (decks.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(decks.size()) - 1);
  }
}

bool FlashcardRecentsActivity::openSelectedDeck() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(decks.size())) {
    return false;
  }

  const auto selectedDeck = decks[selectedIndex];
  FlashcardDeck deck;
  std::string error;
  if (!FLASHCARDS.loadDeck(selectedDeck.path, deck, &error)) {
    transientMessage = error.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : error;
    transientUntilMs = millis() + 1500;
    requestUpdate(true);
    return false;
  }

  startActivityForResult(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, selectedDeck.path),
                         [this](const ActivityResult& result) {
                           reloadDecks();
                           if (const auto* session = std::get_if<FlashcardSessionResult>(&result.data)) {
                             startActivityForResult(std::make_unique<FlashcardSessionSummaryActivity>(renderer, mappedInput, *session),
                                                    [this](const ActivityResult&) {
                                                      reloadDecks();
                                                      requestUpdate();
                                                    });
                             return;
                           }
                           requestUpdate();
                         });
  return true;
}

void FlashcardRecentsActivity::onEnter() {
  Activity::onEnter();
  reloadDecks();
  requestUpdate();
}

void FlashcardRecentsActivity::loop() {
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(decks.size()) &&
        mappedInput.getHeldTime() >= DELETE_RECENT_FLASHCARD_HOLD_MS) {
      const FlashcardDeckRecord selectedDeck = decks[selectedIndex];
      const size_t currentSelection = selectedIndex;
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_RECENTS), selectedDeck.title),
          [this, selectedDeck, currentSelection](const ActivityResult& result) {
            if (!result.isCancelled) {
              FLASHCARDS.removeRecentDeck(selectedDeck.deckId);
              reloadDecks();
              if (decks.empty()) {
                selectedIndex = 0;
              } else if (currentSelection >= decks.size()) {
                selectedIndex = static_cast<int>(decks.size()) - 1;
              } else {
                selectedIndex = static_cast<int>(currentSelection);
              }
            }
            requestUpdate(true);
          });
      return;
    }

    (void)openSelectedDeck();
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

void FlashcardRecentsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_RECENTS));

  if (decks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 24, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(decks.size()), selectedIndex,
                 [this](const int index) { return decks[index].title; },
                 [this](const int index) { return buildDeckSubtitle(decks[index]); },
                 [](const int) { return UIIcon::Text; });
  }

  if (!transientMessage.empty() && millis() < transientUntilMs) {
    renderer.drawCenteredText(SMALL_FONT_ID, contentTop + contentHeight - 18, transientMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), decks.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
