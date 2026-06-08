#include "SokobanActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdlib>

#include "SokobanBoardRenderer.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr char kModule[] = "SOKO";
constexpr char kProgressPath[] = "/.crosspoint/sokoban_progress.txt";

// Tiny "pack path\nlevel index" file -- a JsonDocument would be overkill for
// two fields, and this keeps the feature's footprint minimal.
bool readProgressFile(std::string& outPackPath, int& outLevelIndex) {
  if (!Storage.exists(kProgressPath)) return false;
  const String contents = Storage.readFile(kProgressPath);
  if (contents.isEmpty()) return false;

  const std::string text(contents.c_str());
  const size_t newline = text.find('\n');
  if (newline == std::string::npos) return false;

  outPackPath = text.substr(0, newline);
  outLevelIndex = atoi(text.c_str() + newline + 1);
  return !outPackPath.empty();
}

void writeProgressFile(const std::string& packPath, int levelIndex) {
  Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite(kModule, kProgressPath, file)) return;
  const std::string contents = packPath + "\n" + std::to_string(levelIndex);
  file.write(contents.c_str(), contents.size());
  file.flush();
  file.close();
}

}  // namespace

void SokobanActivity::scanPacks() {
  packPaths = SokobanLevelPack::listPackPaths(levelsDir);
  packSelectorIndex = std::min(packSelectorIndex, packPaths.empty() ? size_t{0} : packPaths.size() - 1);
}

void SokobanActivity::loadProgress() {
  std::string savedPackPath;
  int savedLevelIndex = 0;
  if (!readProgressFile(savedPackPath, savedLevelIndex)) return;

  for (size_t i = 0; i < packPaths.size(); i++) {
    if (packPaths[i] == savedPackPath) {
      packSelectorIndex = i;
      if (openPack(savedPackPath)) {
        loadLevel(savedLevelIndex);
      }
      return;
    }
  }
}

// openPack() always starts a pack at level 0 -- this restores the saved
// level on top of that when the saved progress belongs to this pack, so
// re-opening a pack from the pack list resumes where the player left off.
void SokobanActivity::resumeSavedLevel(const std::string& packPath) {
  std::string savedPackPath;
  int savedLevelIndex = 0;
  if (readProgressFile(savedPackPath, savedLevelIndex) && savedPackPath == packPath) {
    loadLevel(savedLevelIndex);
  }
}

void SokobanActivity::saveProgress() const {
  if (!pack.isOpen()) return;
  writeProgressFile(pack.path(), levelIndex);
}

bool SokobanActivity::openPack(const std::string& path) {
  if (!pack.open(path)) {
    showTransientMessage(tr(STR_SOKOBAN_LOAD_ERROR));
    return false;
  }
  levelIndex = 0;
  return loadLevel(0);
}

bool SokobanActivity::loadLevel(int index) {
  std::vector<std::string> lines;
  if (!pack.loadLevel(index, lines) || !game.loadFromLines(lines)) {
    levelLoaded = false;
    showTransientMessage(tr(STR_SOKOBAN_LOAD_ERROR));
    return false;
  }
  levelIndex = index;
  levelLoaded = true;
  consecutiveUndoPresses = 0;
  return true;
}

void SokobanActivity::goToLevel(int delta) {
  if (!pack.isOpen()) return;
  const int target = std::clamp(levelIndex + delta, 0, pack.levelCount() - 1);
  if (target == levelIndex) return;
  if (loadLevel(target)) {
    saveProgress();
    requestUpdate();
  }
}

void SokobanActivity::restartLevel() {
  if (!levelLoaded) return;
  game.reset();
  consecutiveUndoPresses = 0;
  showTransientMessage(tr(STR_RESTART));
  requestUpdate();
}

void SokobanActivity::confirmRestart() {
  if (!levelLoaded) return;
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_SOKOBAN_RESTART_CONFIRM), ""),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          restartLevel();
        }
        renderer.requestNextFullRefresh();
        requestUpdate(true);
      });
}

void SokobanActivity::toggleControlMode() {
  controlMode = (controlMode == ControlMode::Move) ? ControlMode::LevelNav : ControlMode::Move;
  if (controlMode == ControlMode::LevelNav) {
    showTransientMessage(tr(STR_SOKOBAN_LEVEL_NAV_MODE));
  } else {
    showTransientMessage(tr(STR_SOKOBAN_MOVE_MODE));
  }
}

void SokobanActivity::exitToPackSelect() {
  mode = Mode::PackSelect;
  controlMode = ControlMode::Move;
  consecutiveUndoPresses = 0;
  pack.close();
  levelLoaded = false;
  scanPacks();
  requestUpdate();
}

void SokobanActivity::showTransientMessage(const std::string& message, unsigned long durationMs) {
  transientMessage = message;
  transientUntilMs = millis() + durationMs;
  requestUpdate();
}

void SokobanActivity::onEnter() {
  Activity::onEnter();
  controlMode = ControlMode::Move;
  consecutiveUndoPresses = 0;
  scanPacks();
  loadProgress();
  if (pack.isOpen() && levelLoaded) {
    mode = Mode::Playing;
  } else {
    mode = Mode::PackSelect;
  }
  renderer.requestNextFullRefresh();
  requestUpdate(true);
}

void SokobanActivity::onExit() {
  saveProgress();
  pack.close();
  Activity::onExit();
}

void SokobanActivity::loop() {
  if (!transientMessage.empty() && millis() >= transientUntilMs) {
    transientMessage.clear();
    requestUpdate();
  }

  if (mode == Mode::PackSelect) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!packPaths.empty()) {
        const std::string& path = packPaths[packSelectorIndex];
        if (openPack(path)) {
          resumeSavedLevel(path);
          mode = Mode::Playing;
          renderer.requestNextFullRefresh();
          requestUpdate(true);
        }
      }
      return;
    }

    const int listSize = static_cast<int>(packPaths.size());
    buttonNavigator.onNextRelease([this, listSize] {
      packSelectorIndex = ButtonNavigator::nextIndex(static_cast<int>(packSelectorIndex), listSize);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, listSize] {
      packSelectorIndex = ButtonNavigator::previousIndex(static_cast<int>(packSelectorIndex), listSize);
      requestUpdate();
    });
    return;
  }

  // Mode::Playing
  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= kLongPressMs) {
    lockLongPressBack = true;
    restartLevel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < kLongPressMs) {
      saveProgress();
      exitToPackSelect();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    toggleControlMode();
    return;
  }

  if (lockLongPressConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockLongPressConfirm = false;
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= kLongPressMs) {
    lockLongPressConfirm = true;
    consecutiveUndoPresses = 0;
    confirmRestart();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Mashing Undo usually means the player is stuck rather than fixing one
    // mistake -- offer a restart instead of making them grind it out.
    if (++consecutiveUndoPresses > kUndoRestartThreshold) {
      consecutiveUndoPresses = 0;
      confirmRestart();
    } else if (game.undo()) {
      requestUpdate();
    }
    return;
  }

  if (levelLoaded && game.isSolved()) {
    // Once solved, stop accepting further moves and let the forward button
    // jump straight to the next level -- no need to flip into level-nav mode
    // first, since there's nothing left to do here. The back/previous button
    // is intentionally left inert: its physical direction would otherwise
    // conflict with "go forward to the next puzzle", which is confusing.
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      goToLevel(1);
    }
    return;
  }

  // The side buttons are physically the same as the page-turn buttons, so
  // their meaning depends on controlMode (toggled with Power): either they
  // move the player (Up/Down) or flip between levels (PageBack/PageForward).
  if (controlMode == ControlMode::LevelNav) {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      goToLevel(-1);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      goToLevel(1);
    }
    return;
  }

  if (!levelLoaded) return;

  int dr = 0;
  int dc = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    dr = -1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    dr = 1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    dc = -1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    dc = 1;
  } else {
    return;
  }

  const bool wasSolved = game.isSolved();
  if (game.move(dr, dc)) {
    consecutiveUndoPresses = 0;
    requestUpdate();
    if (!wasSolved && game.isSolved()) {
      showTransientMessage(tr(STR_SOKOBAN_SOLVED), 2500);
    }
  }
}

void SokobanActivity::render(RenderLock&&) {
  if (mode == Mode::PackSelect) {
    renderPackSelect();
  } else {
    renderPlaying();
  }
}

void SokobanActivity::renderPackSelect() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SOKOBAN), tr(STR_SOKOBAN_APP_DESC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (packPaths.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_SOKOBAN_NO_PACKS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(packPaths.size()),
        static_cast<int>(packSelectorIndex),
        [this](const int index) { return SokobanLevelPack::displayNameFromPath(packPaths[index]); });
  }

  if (!transientMessage.empty() && millis() < transientUntilMs) {
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - metrics.buttonHintsHeight - 18, transientMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), packPaths.empty() ? "" : tr(STR_OPEN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SokobanActivity::renderPlaying() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string packName = SokobanLevelPack::displayNameFromPath(pack.path());
  const std::string subtitle =
      "\xe2\x97\x80 " + std::to_string(levelIndex + 1) + "/" + std::to_string(pack.levelCount()) + " \xe2\x96\xb6";
  HeaderDateUtils::drawHeaderWithDate(renderer, packName.c_str(), subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int sideMargin = metrics.contentSidePadding;
  const int statsY = pageHeight - metrics.buttonHintsHeight - 18;

  if (levelLoaded) {
    SokobanBoardRenderer::draw(renderer, game, sideMargin, contentTop, pageWidth - 2 * sideMargin, contentHeight);

    const std::string moveStats = std::to_string(game.moveCount()) + " " + tr(STR_SOKOBAN_MOVES);
    renderer.drawCenteredText(SMALL_FONT_ID, statsY, moveStats.c_str());
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_SOKOBAN_LOAD_ERROR));
  }

  // Sits just above the move counter rather than over the board itself --
  // centered-on-board placement made messages hard to read against the puzzle.
  if (!transientMessage.empty() && millis() < transientUntilMs) {
    renderer.drawCenteredText(SMALL_FONT_ID, statsY - 20, transientMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UNDO), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
