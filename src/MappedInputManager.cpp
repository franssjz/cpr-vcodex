#include "MappedInputManager.h"

#include <string>

#include "CrossPointSettings.h"

namespace {
using ButtonIndex = uint8_t;

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

// Order matches CrossPointSettings::SIDE_BUTTON_LAYOUT.
constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};

bool isAsciiAlphaNum(const unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string sanitizeBackLabel(const char* label) {
  if (label == nullptr || label[0] == '\0') {
    return "";
  }

  std::string text(label);
  bool hadPrefix = false;

  // Handle the real UTF-8 left guillemet prefix first.
  if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xC2 && static_cast<unsigned char>(text[1]) == 0xAB) {
    text.erase(0, 2);
    hadPrefix = true;
  } else if (!text.empty() && static_cast<unsigned char>(text[0]) == 0xAB) {
    // Defensive fallback for any single-byte Latin-1 style prefix.
    text.erase(0, 1);
    hadPrefix = true;
  } else {
    // Several translations contain a mojibake prefix before the actual word.
    // If the first token is short, non-ASCII, and followed by a normal label,
    // strip that token and normalize it to ASCII "<< ".
    const size_t firstSpace = text.find(' ');
    if (firstSpace != std::string::npos && firstSpace > 0 && firstSpace <= 24 && firstSpace + 1 < text.size()) {
      bool hasNonAscii = false;
      bool hasAsciiLetters = false;
      for (size_t i = 0; i < firstSpace; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80) {
          hasNonAscii = true;
        }
        if (isAsciiAlphaNum(c)) {
          hasAsciiLetters = true;
        }
      }

      if (hasNonAscii && !hasAsciiLetters) {
        text.erase(0, firstSpace + 1);
        hadPrefix = true;
      }
    }
  }

  if (hadPrefix) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
      text.erase(text.begin());
    }
    text.insert(0, "<< ");
  }

  return text;
}
}  // namespace

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& side = kSideLayouts[sideLayout];

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const { return mapButton(button, &HalGPIO::wasPressed); }

void MappedInputManager::armConfirmReleaseGuard() const { suppressConfirmReleaseUntilButtonUp = true; }

bool MappedInputManager::wasReleased(const Button button) const {
  if (button == Button::Confirm && suppressConfirmReleaseUntilButtonUp) {
    if (!isPressed(Button::Confirm)) {
      suppressConfirmReleaseUntilButtonUp = false;
    }
    return false;
  }
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  thread_local std::string sanitized[4];

  sanitized[0] = sanitizeBackLabel(back);
  sanitized[1] = confirm ? confirm : "";
  sanitized[2] = previous ? previous : "";
  sanitized[3] = next ? next : "";

  auto labelForHardware = [&](uint8_t hw) -> const char* {
    if (hw == SETTINGS.frontButtonBack) {
      return sanitized[0].c_str();
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return sanitized[1].c_str();
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return sanitized[2].c_str();
    }
    if (hw == SETTINGS.frontButtonRight) {
      return sanitized[3].c_str();
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
