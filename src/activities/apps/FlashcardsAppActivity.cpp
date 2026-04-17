#include "FlashcardsAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "FlashcardsStore.h"
#include "FlashcardBrowserActivity.h"
#include "FlashcardRecentsActivity.h"
#include "FlashcardSettingsActivity.h"
#include "FlashcardStatsActivity.h"
#include "components/UITheme.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int ACTION_COUNT = 4;

bool hasStatsToShow(const FlashcardDeckRecord& record) {
  return record.sessionCount > 0 || record.seenCards > 0 || record.totalReviewed > 0 || record.totalCorrect > 0 ||
         record.totalWrong > 0 || record.totalSkipped > 0 || record.lastReviewedAt > 0;
}

std::string getOpenSubtitle() { return tr(STR_FLASHCARDS_OPEN_DESC); }

std::string getRecentsSubtitle(const int recentCount) { return std::to_string(recentCount); }

std::string getStatsSubtitle(const int deckCount) { return std::to_string(deckCount); }

std::string getSettingsSubtitle() {
  std::string studyModeLabel;
  switch (SETTINGS.flashcardStudyMode) {
    case CrossPointSettings::FLASHCARD_STUDY_DUE:
      studyModeLabel = tr(STR_DUE);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_INFINITE:
      studyModeLabel = tr(STR_RANDOM);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_SCHEDULED:
    default:
      studyModeLabel = tr(STR_SCHEDULED);
      break;
  }

  if (SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_INFINITE) {
    return studyModeLabel;
  }

  return studyModeLabel + " | " +
         (SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_ALL
              ? std::string(tr(STR_ALL))
              : std::to_string(SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_10
                                   ? 10
                                   : SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_20
                                         ? 20
                                         : SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_30 ? 30
                                                                                                                       : 50));
}
}  // namespace

void FlashcardsAppActivity::refreshCounts() {
  recentCount = static_cast<int>(FLASHCARDS.getRecentDecks().size());
  deckCount = 0;
  for (const auto& record : FLASHCARDS.getKnownDecks()) {
    if (hasStatsToShow(record)) {
      deckCount++;
    }
  }
  selectedIndex = std::clamp(selectedIndex, 0, ACTION_COUNT - 1);
}

void FlashcardsAppActivity::openSelectedEntry() {
  std::unique_ptr<Activity> activity;
  switch (selectedIndex) {
    case 0:
      activity = std::make_unique<FlashcardBrowserActivity>(renderer, mappedInput);
      break;
    case 1:
      activity = std::make_unique<FlashcardRecentsActivity>(renderer, mappedInput);
      break;
    case 2:
      activity = std::make_unique<FlashcardStatsActivity>(renderer, mappedInput);
      break;
    default:
      activity = std::make_unique<FlashcardSettingsActivity>(renderer, mappedInput);
      break;
  }

  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    refreshCounts();
    requestUpdate();
  });
}

void FlashcardsAppActivity::onEnter() {
  Activity::onEnter();
  refreshCounts();
  requestUpdate();
}

void FlashcardsAppActivity::loop() {
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedEntry();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ACTION_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ACTION_COUNT);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, ACTION_COUNT, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, ACTION_COUNT, pageItems);
    requestUpdate();
  });
}

void FlashcardsAppActivity::render(RenderLock&&) {
  refreshCounts();
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS),
                                      std::to_string(deckCount).c_str());

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, ACTION_COUNT, selectedIndex,
               [](const int index) {
                 switch (index) {
                   case 0:
                     return std::string(tr(STR_OPEN));
                   case 1:
                     return std::string(tr(STR_RECENTS));
                   case 2:
                     return std::string(tr(STR_STATISTICS));
                   default:
                     return std::string(tr(STR_SETTINGS_TITLE));
                 }
               },
               [this](const int index) {
                 switch (index) {
                   case 0:
                     return getOpenSubtitle();
                   case 1:
                     return getRecentsSubtitle(recentCount);
                   case 2:
                     return getStatsSubtitle(deckCount);
                   default:
                     return getSettingsSubtitle();
                 }
               },
               [](const int index) {
                 switch (index) {
                   case 0:
                     return UIIcon::Folder;
                   case 1:
                     return UIIcon::Recent;
                   case 2:
                     return UIIcon::Library;
                   default:
                     return UIIcon::Settings;
                 }
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
