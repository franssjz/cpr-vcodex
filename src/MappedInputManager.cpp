#include "MappedInputManager.h"

#include "CrossPointSettings.h"

#include <string>
#include <utility>

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

bool isReaderLandscapeOrientation(const uint8_t orientation) {
  return orientation == CrossPointSettings::LANDSCAPE_CW || orientation == CrossPointSettings::LANDSCAPE_CCW;
}

ButtonIndex invertFrontButtonPosition(const ButtonIndex button) {
  switch (button) {
    case HalGPIO::BTN_BACK:
      return HalGPIO::BTN_RIGHT;
    case HalGPIO::BTN_CONFIRM:
      return HalGPIO::BTN_LEFT;
    case HalGPIO::BTN_LEFT:
      return HalGPIO::BTN_CONFIRM;
    case HalGPIO::BTN_RIGHT:
      return HalGPIO::BTN_BACK;
    default:
      return button;
  }
}

ButtonIndex mapFrontButtonForReaderOrientation(const ButtonIndex button, const ButtonIndex leftButton,
                                               const ButtonIndex rightButton, const bool readerMode,
                                               const uint8_t orientation) {
  if (!readerMode) {
    return button;
  }

  const auto orientationMode =
      static_cast<CrossPointSettings::FRONT_BUTTON_ORIENTATION_AWARE>(SETTINGS.frontButtonOrientationAware);

  if (orientationMode == CrossPointSettings::FRONT_ORIENTATION_AWARE_ALL_BUTTONS &&
      orientation == CrossPointSettings::INVERTED) {
    return invertFrontButtonPosition(button);
  }

  return button;
}

SideLayoutMap mapSideLayoutForReaderOrientation(SideLayoutMap side, const bool readerMode, const uint8_t orientation) {
  if (readerMode && SETTINGS.sideButtonOrientationAware && isReaderLandscapeOrientation(orientation)) {
    std::swap(side.pageBack, side.pageForward);
  }
  return side;
}

bool isAsciiAlphaNum(const unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string sanitizeBackLabel(const char* label) {
  if (label == nullptr || label[0] == '\0') {
    return "";
  }

  std::string text(label);
  bool hadPrefix = false;

  if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xC2 &&
      static_cast<unsigned char>(text[1]) == 0xAB) {
    text.erase(0, 2);
    hadPrefix = true;
  } else if (!text.empty() && static_cast<unsigned char>(text[0]) == 0xAB) {
    text.erase(0, 1);
    hadPrefix = true;
  } else {
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
  const auto side = mapSideLayoutForReaderOrientation(kSideLayouts[sideLayout], readerMode, readerOrientation);
  const bool useReaderMapping = readerMode && SETTINGS.readerFrontButtonsEnabled;
  const ButtonIndex btnBack = useReaderMapping ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
  const ButtonIndex btnConfirm = useReaderMapping ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
  const ButtonIndex btnLeft = useReaderMapping ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
  const ButtonIndex btnRight = useReaderMapping ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;
  const ButtonIndex mappedBack =
      mapFrontButtonForReaderOrientation(btnBack, btnLeft, btnRight, readerMode, readerOrientation);
  const ButtonIndex mappedConfirm =
      mapFrontButtonForReaderOrientation(btnConfirm, btnLeft, btnRight, readerMode, readerOrientation);
  const ButtonIndex mappedLeft = mapFrontButtonForReaderOrientation(btnLeft, btnLeft, btnRight, readerMode, readerOrientation);
  const ButtonIndex mappedRight =
      mapFrontButtonForReaderOrientation(btnRight, btnLeft, btnRight, readerMode, readerOrientation);

  switch (button) {
    case Button::Back:
      return (gpio.*fn)(mappedBack);
    case Button::Confirm:
      return (gpio.*fn)(mappedConfirm);
    case Button::Left:
      return (gpio.*fn)(mappedLeft);
    case Button::Right:
      return (gpio.*fn)(mappedRight);
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

bool MappedInputManager::isAnyMappedButtonPressed() const {
  return isPressed(Button::Back) || isPressed(Button::Confirm) || isPressed(Button::Left) ||
         isPressed(Button::Right) || isPressed(Button::Up) || isPressed(Button::Down) ||
         isPressed(Button::Power) || isPressed(Button::PageBack) || isPressed(Button::PageForward);
}

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  thread_local std::string sanitized[4];

  sanitized[0] = sanitizeBackLabel(back);
  sanitized[1] = confirm ? confirm : "";
  sanitized[2] = previous ? previous : "";
  sanitized[3] = next ? next : "";

  const bool useReaderMapping = readerMode && SETTINGS.readerFrontButtonsEnabled;
  const ButtonIndex btnBack = useReaderMapping ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
  const ButtonIndex btnConfirm = useReaderMapping ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
  const ButtonIndex btnLeft = useReaderMapping ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
  const ButtonIndex btnRight = useReaderMapping ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;
  const ButtonIndex mappedBack = mapFrontButtonForReaderOrientation(btnBack, btnLeft, btnRight, readerMode, readerOrientation);
  const ButtonIndex mappedConfirm =
      mapFrontButtonForReaderOrientation(btnConfirm, btnLeft, btnRight, readerMode, readerOrientation);
  const ButtonIndex mappedLeft = mapFrontButtonForReaderOrientation(btnLeft, btnLeft, btnRight, readerMode, readerOrientation);
  const ButtonIndex mappedRight = mapFrontButtonForReaderOrientation(btnRight, btnLeft, btnRight, readerMode, readerOrientation);

  auto labelForHardware = [&](uint8_t hw) -> const char* {
    if (hw == mappedBack) {
      return sanitized[0].c_str();
    }
    if (hw == mappedConfirm) {
      return sanitized[1].c_str();
    }
    if (hw == mappedLeft) {
      return sanitized[2].c_str();
    }
    if (hw == mappedRight) {
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
