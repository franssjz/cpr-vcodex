#include "ConfirmationActivity.h"

#include <I18n.h>

#include "../../components/UITheme.h"
#include "HalDisplay.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(fontId);
  const int maxWidth = renderer.getScreenWidth() - (margin * 2);

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
  }
  // The body wraps rather than truncating: a prompt explaining what is about to
  // happen is routinely longer than one line, and truncating it cut off the part
  // that mattered. The heading stays single-line — it's a title, not prose.
  //
  // A newline in the body starts a new paragraph, so callers can separate the
  // question being asked from the explanation of what it means instead of
  // running them together as one block.
  if (!body.empty()) {
    size_t start = 0;
    while (start <= body.size() && static_cast<int>(bodyLines.size()) < maxBodyLines) {
      const size_t newline = body.find('\n', start);
      const std::string paragraph =
          body.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
      if (paragraph.empty()) {
        bodyLines.emplace_back();  // blank line between paragraphs
      } else {
        const int remaining = maxBodyLines - static_cast<int>(bodyLines.size());
        const auto wrapped =
            renderer.wrappedText(fontId, paragraph.c_str(), maxWidth, remaining, EpdFontFamily::REGULAR);
        bodyLines.insert(bodyLines.end(), wrapped.begin(), wrapped.end());
      }
      if (newline == std::string::npos) break;
      start = newline + 1;
    }
  }

  int totalHeight = 0;
  if (!safeHeading.empty()) totalHeight += lineHeight;
  totalHeight += static_cast<int>(bodyLines.size()) * lineHeight;
  if (!safeHeading.empty() && !bodyLines.empty()) totalHeight += spacing;

  startY = (renderer.getScreenHeight() - totalHeight) / 2;

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  int currentY = startY;
  LOG_DBG("CONF", "currentY: %d", currentY);
  // Draw Heading
  if (!safeHeading.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeHeading.c_str(), true, EpdFontFamily::BOLD);
    currentY += lineHeight + spacing;
  }

  // Draw Body
  for (const auto& line : bodyLines) {
    renderer.drawCenteredText(fontId, currentY, line.c_str(), true, EpdFontFamily::REGULAR);
    currentY += lineHeight;
  }

  // Draw UI Elements
  const auto labels = mappedInput.mapLabels("", "", I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
}