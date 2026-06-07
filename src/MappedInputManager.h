#pragma once

#include <HalGPIO.h>

#include <cstdint>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void setReaderMode(bool enabled) { readerMode = enabled; }
  void setReaderOrientation(uint8_t orientation) { readerOrientation = orientation; }
  void update() const;
  void armConfirmReleaseGuard() const;
  void armPressedButtonsReleaseGuard() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  bool isAnyMappedButtonPressed() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  HalGPIO& gpio;
  bool readerMode = false;
  uint8_t readerOrientation = 0;
  mutable uint16_t suppressRawUntilReleaseMask = 0;

  uint8_t rawButtonFor(Button button) const;
  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool isSuppressedUntilRelease(Button button) const;
  bool hasSuppressedRawButtonHeld() const;
  bool consumeSuppressedRawInput() const;
  static uint16_t rawButtonMask(uint8_t rawButton);
};
