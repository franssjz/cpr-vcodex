#include "IfFoundActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr const char* IF_FOUND_FILE = "/if_found.txt";
constexpr size_t IF_FOUND_MAX_BYTES = 8192;

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string toLowerAscii(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return value;
}

std::string basenameFromRootEntry(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  const auto slash = value.find_last_of('/');
  if (slash != std::string::npos) {
    value = value.substr(slash + 1);
  }
  return value;
}

bool isIfFoundCandidate(const std::string& filename) {
  const std::string lower = toLowerAscii(basenameFromRootEntry(filename));
  return lower == "if_found.txt" || lower == "if_found.txt.txt";
}

std::string findIfFoundPath() {
  if (Storage.exists(IF_FOUND_FILE)) {
    return IF_FOUND_FILE;
  }

  const auto rootFiles = Storage.listFiles("/", 128);
  for (const auto& file : rootFiles) {
    std::string filename = file.c_str();
    if (!isIfFoundCandidate(filename)) {
      continue;
    }
    if (filename.empty() || filename.front() != '/') {
      filename = "/" + filename;
    }
    return filename;
  }

  return "";
}

std::string readSmallTextFile(const std::string& path) {
  if (path.empty()) {
    return "";
  }

  FsFile file;
  if (!Storage.openFileForRead("IFF", path, file)) {
    return "";
  }

  std::string content;
  content.reserve(std::min(IF_FOUND_MAX_BYTES, static_cast<size_t>(file.fileSize())));
  uint8_t buffer[128];
  while (file.available() && content.size() < IF_FOUND_MAX_BYTES) {
    const size_t remaining = IF_FOUND_MAX_BYTES - content.size();
    const size_t toRead = std::min(remaining, sizeof(buffer));
    const int read = file.read(buffer, toRead);
    if (read <= 0) {
      break;
    }
    content.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(read));
  }

  file.close();
  return content;
}

void appendUtf8(std::string& out, const uint32_t codepoint) {
  if (codepoint == 0) {
    return;
  }
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

uint16_t readUtf16Unit(const std::string& value, const size_t offset, const bool littleEndian) {
  const uint16_t first = static_cast<uint8_t>(value[offset]);
  const uint16_t second = static_cast<uint8_t>(value[offset + 1]);
  return littleEndian ? static_cast<uint16_t>(first | (second << 8)) : static_cast<uint16_t>((first << 8) | second);
}

std::string decodeUtf16(const std::string& value, const bool littleEndian, size_t offset) {
  std::string out;
  out.reserve(value.size() / 2);
  while (offset + 1 < value.size()) {
    uint32_t codepoint = readUtf16Unit(value, offset, littleEndian);
    offset += 2;

    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && offset + 1 < value.size()) {
      const uint16_t low = readUtf16Unit(value, offset, littleEndian);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
        offset += 2;
      }
    }

    appendUtf8(out, codepoint);
  }
  return out;
}

bool looksLikeUtf16Le(const std::string& value) {
  return value.size() >= 4 && value[1] == '\0' && value[3] == '\0';
}

bool looksLikeUtf16Be(const std::string& value) {
  return value.size() >= 4 && value[0] == '\0' && value[2] == '\0';
}

std::string normalizeTextEncoding(std::string value) {
  if (value.size() >= 3 && static_cast<uint8_t>(value[0]) == 0xEF && static_cast<uint8_t>(value[1]) == 0xBB &&
      static_cast<uint8_t>(value[2]) == 0xBF) {
    value.erase(0, 3);
  } else if (value.size() >= 2 && static_cast<uint8_t>(value[0]) == 0xFF &&
             static_cast<uint8_t>(value[1]) == 0xFE) {
    value = decodeUtf16(value, true, 2);
  } else if (value.size() >= 2 && static_cast<uint8_t>(value[0]) == 0xFE &&
             static_cast<uint8_t>(value[1]) == 0xFF) {
    value = decodeUtf16(value, false, 2);
  } else if (looksLikeUtf16Le(value)) {
    value = decodeUtf16(value, true, 0);
  } else if (looksLikeUtf16Be(value)) {
    value = decodeUtf16(value, false, 0);
  }

  return value;
}

std::string normalizeNewlines(std::string value) {
  size_t pos = 0;
  while ((pos = value.find("\r\n", pos)) != std::string::npos) {
    value.replace(pos, 2, "\n");
  }
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
  return value;
}

std::vector<std::string> splitLinesPreserveEmpty(const std::string& value) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t end = value.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(value.substr(start));
      break;
    }
    lines.push_back(value.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}
}  // namespace

void IfFoundActivity::loadContent() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int textWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - metrics.scrollBarWidth -
                        metrics.scrollBarRightOffset - 8;

  introLines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_IF_FOUND_MESSAGE), textWidth, 8, EpdFontFamily::REGULAR);

  std::string body = normalizeNewlines(normalizeTextEncoding(readSmallTextFile(findIfFoundPath())));
  if (trim(body).empty()) {
    body = tr(STR_IF_FOUND_FILE_HINT);
  }

  bodyLines.clear();
  for (const auto& rawLine : splitLinesPreserveEmpty(body)) {
    if (trim(rawLine).empty()) {
      bodyLines.emplace_back("");
      continue;
    }

    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, rawLine.c_str(), textWidth, 64, EpdFontFamily::BOLD);
    if (wrapped.empty()) {
      bodyLines.push_back(rawLine);
      continue;
    }
    bodyLines.insert(bodyLines.end(), wrapped.begin(), wrapped.end());
  }

  while (!bodyLines.empty() && bodyLines.back().empty()) {
    bodyLines.pop_back();
  }

  scrollOffset = std::clamp(scrollOffset, 0, getMaxScrollOffset());
}

int IfFoundActivity::getVisibleBodyLineCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int introHeight = static_cast<int>(introLines.size()) * lineHeight;
  const int bodyTop = contentTop + introHeight + 16;
  const int viewportHeight = pageHeight - bodyTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return std::max(1, viewportHeight / lineHeight);
}

int IfFoundActivity::getMaxScrollOffset() const {
  return std::max(0, static_cast<int>(bodyLines.size()) - getVisibleBodyLineCount());
}

void IfFoundActivity::onEnter() {
  Activity::onEnter();
  loadContent();
  requestUpdate();
}

void IfFoundActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right, MappedInputManager::Button::Down}, [this] {
    const int maxScrollOffset = getMaxScrollOffset();
    if (maxScrollOffset <= 0) {
      return;
    }
    scrollOffset = std::min(scrollOffset + 1, maxScrollOffset);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left, MappedInputManager::Button::Up}, [this] {
    if (scrollOffset <= 0) {
      return;
    }
    scrollOffset = std::max(0, scrollOffset - 1);
    requestUpdate();
  });
}

void IfFoundActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_IF_FOUND_RETURN_ME));

  int currentY = contentTop;
  for (const auto& line : introLines) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, currentY, line.c_str(), true, EpdFontFamily::REGULAR);
    currentY += lineHeight;
  }

  currentY += 8;
  renderer.drawLine(sidePadding, currentY, pageWidth - sidePadding, currentY);
  currentY += 8;

  const int bodyTop = currentY;
  const int viewportHeight = pageHeight - bodyTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleLines = std::max(1, viewportHeight / lineHeight);
  const int endLine = std::min(static_cast<int>(bodyLines.size()), scrollOffset + visibleLines);

  int textY = bodyTop;
  for (int i = scrollOffset; i < endLine; ++i) {
    if (!bodyLines[i].empty()) {
      renderer.drawText(UI_10_FONT_ID, sidePadding, textY, bodyLines[i].c_str(), true, EpdFontFamily::BOLD);
    }
    textY += lineHeight;
  }

  if (static_cast<int>(bodyLines.size()) > visibleLines) {
    const int scrollTrackX = pageWidth - metrics.scrollBarRightOffset;
    const int maxScrollOffset = getMaxScrollOffset();
    const int scrollBarHeight = std::max(18, (viewportHeight * visibleLines) / static_cast<int>(bodyLines.size()));
    const int scrollBarY =
        bodyTop + ((viewportHeight - scrollBarHeight) * std::min(scrollOffset, maxScrollOffset)) / maxScrollOffset;
    renderer.drawLine(scrollTrackX, bodyTop, scrollTrackX, bodyTop + viewportHeight, true);
    renderer.fillRect(scrollTrackX - metrics.scrollBarWidth + 1, scrollBarY, metrics.scrollBarWidth, scrollBarHeight,
                      true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
