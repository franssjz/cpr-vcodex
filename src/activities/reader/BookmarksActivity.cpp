#include "BookmarksActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* PAGE_LABEL = "Page ";
constexpr unsigned long DELETE_BOOKMARK_HOLD_MS = 1000;
}  // namespace

std::string BookmarksActivity::getItemLabel(const int index) const {
  const auto& bookmark = bookmarks[index];
  char buffer[64];

  if (bookmark.isTextHighlight) {
    snprintf(buffer, sizeof(buffer), "%d. %s", index + 1, tr(STR_TEXT_HIGHLIGHT_PREFIX));
    return std::string(buffer) + bookmark.snippet;
  }

  if (!bookmark.snippet.empty()) {
    snprintf(buffer, sizeof(buffer), "%d. %s", index + 1, tr(STR_PAGE_MARK_PREFIX));
    return std::string(buffer) + bookmark.snippet;
  }

  if (epub) {
    const int tocIndex = epub->getTocIndexForSpineIndex(bookmark.spineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      snprintf(buffer, sizeof(buffer), "%d. ", index + 1);
      return std::string(buffer) + tocItem.title + " - " + PAGE_LABEL + std::to_string(bookmark.pageNumber + 1);
    }

    snprintf(buffer, sizeof(buffer), "%d. %s%d, %s%d", index + 1, tr(STR_SECTION_PREFIX), bookmark.spineIndex + 1,
             PAGE_LABEL, bookmark.pageNumber + 1);
    return buffer;
  }

  snprintf(buffer, sizeof(buffer), "%d. %s%d", index + 1, PAGE_LABEL, bookmark.pageNumber + 1);
  return buffer;
}

void BookmarksActivity::refreshRowWindow(const int start) {
  const int first = std::max(0, std::min(start, listCount()));
  if (first == rowWindowStart) return;
  rowWindowStart = first;
  rowWindowCount = std::min(ROW_WINDOW, listCount() - first);
  for (int i = 0; i < rowWindowCount; ++i) {
    rowLabels[i] = getItemLabel(first + i);
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    item.actionValue = static_cast<int16_t>(first + i);
    rowItems[i] = item;
  }

  // Wrapped snippets measure many prefixes before drawing. Load their glyphs
  // together so each prefix does not grow/rebuild the SD font's mini-cache.
  // Start at the viewport itself, including near the end of the list, so an
  // off-screen earlier row cannot consume the heap-adaptive glyph budget.
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* { return static_cast<const std::string*>(ctx)[i].c_str(); },
      rowLabels, static_cast<uint32_t>(rowWindowCount));
}

void BookmarksActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<BookmarksActivity*>(ctx);
  const int slot = static_cast<int>(index) - self->rowWindowStart;
  if (slot < 0 || slot >= self->rowWindowCount) return;
  item = self->rowItems[slot];
  // A dense window can exceed the fallback cache's glyph/heap budget. Ensure
  // this whole label AND the truncation ellipsis are resident before wrapping
  // probes prefixes. A single-string prewarm omits the ellipsis; adding it to
  // an already-full cache would evict the label and restart prefix-by-prefix
  // loading. The getter overload includes it. Usually this is a cache hit.
  // On a miss, replace rather than re-reading a growing union of earlier rows:
  // dense highlights may exceed the cache even within one viewport.
  self->renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* label, uint32_t) -> const char* { return static_cast<const char*>(label); }, item.label, 1,
      EpdFontFamily::REGULAR, /*accumulate=*/false);
}

const char* BookmarksActivity::headerTitle() const {
  return headerTitleText.empty() ? tr(STR_HIGHLIGHTS) : headerTitleText.c_str();
}

void BookmarksActivity::onEnter() {
  // Release retained reader glyph arenas under pressure before allocating the
  // list window, as the chapter list does. The reader rebuilds them on return.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  rowWindowStart = -1;
  rowWindowCount = 0;
  UiListActivity::onEnter();
}

void BookmarksActivity::onExit() {
  Activity::onExit();
  // rowItems' labels alias rowLabels; drop both together.
  for (int i = 0; i < ROW_WINDOW; ++i) {
    rowItems[i] = {};
    rowLabels[i].clear();
  }
  rowWindowStart = -1;
  rowWindowCount = 0;
}

void BookmarksActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BookmarksActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  // Opening the bookmark leaves this screen; a lingering flash would gray an
  // unrelated row when the list next appears.
  app.clearTapFlash();
  const auto& bookmark = bookmarks[index];
  setResult(BookmarkResult{static_cast<int>(bookmark.spineIndex), bookmark.pageNumber, bookmark.hasVisibleTextOffset,
                           bookmark.visibleTextOffset});
  finish();
}

void BookmarksActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  confirmDeleteBookmark(index);
}

bool BookmarksActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!bookmarks.empty() && nav.selected >= 0 && nav.selected < listCount()) {
      if (mappedInput.getHeldTime() >= DELETE_BOOKMARK_HOLD_MS) {
        confirmDeleteBookmark(nav.selected);
      } else {
        activateIndex(nav.selected);
      }
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finishCancelled();
    return true;
  }

  return false;
}

void BookmarksActivity::confirmDeleteBookmark(const int index) {
  if (!onDeleteBookmark || index < 0 || index >= listCount()) {
    return;
  }

  const auto bookmark = bookmarks[index];
  const std::string body = getItemLabel(index);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_HIGHLIGHT), body),
      [this, bookmark](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        if (onDeleteBookmark(bookmark)) {
          // The interaction table still indexes the pre-removal rows; stop
          // routing touches against it until the next render republishes.
          closeRouting();
          bookmarks.erase(std::remove_if(bookmarks.begin(), bookmarks.end(),
                                         [&](const BookmarkStore::Bookmark& current) {
                                           return current.isTextHighlight == bookmark.isTextHighlight &&
                                                  current.spineIndex == bookmark.spineIndex &&
                                                  current.pageNumber == bookmark.pageNumber &&
                                                  current.endPageNumber == bookmark.endPageNumber &&
                                                  current.startWordIndex == bookmark.startWordIndex &&
                                                  current.endWordIndex == bookmark.endWordIndex &&
                                                  current.snippet == bookmark.snippet &&
                                                  current.hasVisibleTextOffset == bookmark.hasVisibleTextOffset &&
                                                  (!current.hasVisibleTextOffset ||
                                                   current.visibleTextOffset == bookmark.visibleTextOffset);
                                         }),
                          bookmarks.end());

          if (bookmarks.empty()) {
            finishCancelled();
            return;
          }

          rowWindowStart = -1;
          if (nav.selected >= listCount()) {
            nav.selected = listCount() - 1;
          }
          nav.follow(listCount());
        }

        requestUpdate(true);
      });
}

void BookmarksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (bookmarks.empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(bookmarks.size());
  props.action = ACTION_ROW;
  // Tap opens; long-press prompts deletion (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  // Small font so more of a snippet fits; maxLines=2 wraps long highlights and
  // marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  // The shared navigation first estimates using single-line rows. For a jump
  // to the end of wrapped highlights, start with the measured page size so we
  // do not paint a whole off-screen window just to discover that the selected
  // row needs a second build. Layout feedback still corrects varying heights.
  if (nav.followPending && nav.trusts(listCount()) && props.selectedIndex >= nav.top + nav.drawnRows) {
    nav.top = props.selectedIndex - nav.drawnRows + 1;
    props.topIndex = static_cast<uint16_t>(nav.top);
  }
  refreshRowWindow(nav.top);
  props.items = rowItems;
  props.itemsWindowFirst = static_cast<uint16_t>(rowWindowStart);
  props.itemsWindowCount = static_cast<uint16_t>(rowWindowCount);
  props.rowProvider = &BookmarksActivity::provideRow;
  props.rowProviderCtx = this;
  screen.list(props);
}
