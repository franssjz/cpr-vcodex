#pragma once

#include <GfxRenderer.h>

#include <string>

#include "components/themes/BaseTheme.h"

namespace AppMetricCard {

enum class LabelMode {
  Simple,
  Truncate,
  Wrap,
};

struct Options {
  int paddingX = 12;
  int contentInset = 24;
  int valueLargeY = 14;
  int valueSmallY = 18;
  int labelY = 42;
  int labelMaxLines = 2;
  bool shrinkValue = true;
  bool showCheck = false;
  LabelMode labelMode = LabelMode::Wrap;
};

void draw(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
          const Options& options = Options{});

}  // namespace AppMetricCard
