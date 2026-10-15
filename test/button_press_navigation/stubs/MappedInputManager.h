#pragma once

#include <cstdint>

extern unsigned long testNowMs;
inline unsigned long millis() { return testNowMs; }

class MappedInputManager {
 public:
  enum class Button { NavPrevious, NavNext, Left = NavPrevious, Right = NavNext };
  struct Frame {
    uint32_t heldMs = 0;
    uint8_t pressed = 0;
    uint8_t released = 0;
    uint8_t held = 0;
  } frame;
  mutable uint32_t longPressQueries[2] = {};
  void sample(Frame value) {
    frame = value;
    // Production update() rearms each logical button after it is sampled up.
    longPressed &= frame.held;
  }
  bool wasPressed(Button button) const { return frame.pressed & (1u << static_cast<unsigned>(button)); }
  bool wasReleased(Button button) const { return frame.released & (1u << static_cast<unsigned>(button)); }
  bool isPressed(Button button) const { return frame.held & (1u << static_cast<unsigned>(button)); }
  unsigned long getHeldTime() const { return frame.heldMs; }
  bool wasLongPressed(Button button, unsigned long threshold) const {
    ++longPressQueries[static_cast<unsigned>(button)];
    const auto mask = 1u << static_cast<unsigned>(button);
    if (!isPressed(button)) return false;
    if (frame.heldMs < threshold || (longPressed & mask)) return false;
    longPressed |= mask;
    return true;
  }

 private:
  mutable uint8_t longPressed = 0;
};
