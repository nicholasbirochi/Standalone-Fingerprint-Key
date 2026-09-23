// USB HID keyboard output.
//
// To the host this device is an ordinary keyboard, which is the whole trick:
// no driver, no daemon, no kernel extension. It is also the whole limitation,
// because a keyboard can only send keystrokes -- see docs/security.md.

#pragma once

#include <Arduino.h>

namespace fpkey {

class HidTyper {
 public:
  void Begin();

  // True once the host has enumerated the device. Typing before this silently
  // goes nowhere.
  bool Ready() const;

  // Taps a modifier so the display wakes and the login field takes focus.
  // A modifier rather than a character, so it is harmless if the machine was
  // already awake with a text field focused.
  void WakeHost();

  // Types the secret and presses Return. Characters are paced because the macOS
  // login window drops keystrokes that arrive faster than it redraws.
  void TypeSecret(const char* secret);

  // Control-Command-Q, the macOS lock shortcut.
  void LockHost();

 private:
  bool begun_ = false;
};

}  // namespace fpkey
