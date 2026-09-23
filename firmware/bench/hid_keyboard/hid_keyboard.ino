// Step 2 of the bench check: will this Mac accept the board as a keyboard?
//
// Press BOOT and a line of text appears in whatever window has focus. That is
// the whole premise of the project -- no driver, no daemon, just a keyboard --
// so if this does not work, nothing downstream will.
//
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot = Enabled,
// USB Mode = USB-OTG (TinyUSB). The default "Hardware CDC and JTAG" gives you
// a board that enumerates and a keyboard that silently types nothing.
//
// FOCUS A TEXT EDITOR BEFORE PRESSING THE BUTTON. This types wherever the
// cursor is, including terminals and password fields.
//
// This is a USB HID test and nothing more. It reads no fingerprint, holds no
// secret, and does not unlock anything.

#include <Arduino.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

namespace {

constexpr int kButtonPin = 0;  // BOOT
USBHIDKeyboard keyboard;
bool previous = HIGH;

}  // namespace

void setup() {
  pinMode(kButtonPin, INPUT_PULLUP);
  keyboard.begin();
  USB.begin();
  // The host needs a moment to enumerate the interface; typing before it has
  // finished goes nowhere.
  delay(2000);
}

void loop() {
  const bool current = digitalRead(kButtonPin);
  if (previous == HIGH && current == LOW) {
    keyboard.println("ESP32-S3 working as a USB keyboard");
    delay(300);  // crude debounce; this is a bench sketch
  }
  previous = current;
  delay(20);
}
