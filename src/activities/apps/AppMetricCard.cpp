#include "AppMetricCard.h"

#include "fontIds.h"

namespace {
void drawCheckBadge(GfxRenderer& renderer, const int x, const int y) {
  renderer.fillRect(x, y, 18, 18, true);
  renderer.drawLine(x + 4, y + 10, x + 7, y + 13, 2, false);
  renderer.drawLine(x + 7, y + 13, x + 13, y + 5, 2, false);
}
}  // namespace

namespace AppMetricCard {

void draw(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
          const Options& options) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int textWidth = rect.width - options.contentInset;
  const int valueFontId =
      options.shrinkValue &&
              renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD) > textWidth
          ? UI_10_FONT_ID
          : UI_12_FONT_ID;
  const std::string truncatedValue =
      renderer.truncatedText(valueFontId, value.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(valueFontId, rect.x + options.paddingX,
                    rect.y + (valueFontId == UI_12_FONT_ID ? options.valueLargeY : options.valueSmallY),
                    truncatedValue.c_str(), true, EpdFontFamily::BOLD);

  if (options.labelMode == LabelMode::Simple) {
    renderer.drawText(UI_10_FONT_ID, rect.x + options.paddingX, rect.y + options.labelY, label);
  } else if (options.labelMode == LabelMode::Truncate) {
    const std::string truncatedLabel =
        renderer.truncatedText(UI_10_FONT_ID, label, textWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rect.x + options.paddingX, rect.y + options.labelY, truncatedLabel.c_str());
  } else {
    const auto labelLines =
        renderer.wrappedText(UI_10_FONT_ID, label, textWidth, options.labelMaxLines, EpdFontFamily::REGULAR);
    int labelY = rect.y + options.labelY;
    for (const auto& line : labelLines) {
      renderer.drawText(UI_10_FONT_ID, rect.x + options.paddingX, labelY, line.c_str());
      labelY += renderer.getLineHeight(UI_10_FONT_ID);
    }
  }

  if (options.showCheck) {
    drawCheckBadge(renderer, rect.x + rect.width - 28, rect.y + 40);
  }
}

}  // namespace AppMetricCard
