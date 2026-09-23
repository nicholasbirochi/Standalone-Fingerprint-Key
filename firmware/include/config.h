// Build-time configuration for Standalone-Fingerprint-Key.
//
// Pin assignments assume a small ESP32-S3 dev board with native USB. If yours
// routes things differently, only the pins below need to change; nothing else
// in the firmware hard-codes a GPIO.

#pragma once

#include <Arduino.h>

namespace fpkey {

// --------------------------------------------------------------------------- //
// Pins
// --------------------------------------------------------------------------- //

// UART to the HLK-ZW111. RX is the ESP32 input, so it goes to the module's TX.
constexpr int8_t kSensorRxPin = 4;
constexpr int8_t kSensorTxPin = 5;

// The module's touch output goes high while a finger rests on the platen. Set
// to -1 if you did not wire it; the firmware then polls the image sensor, which
// works but costs roughly 15 mA of idle current.
constexpr int8_t kSensorTouchPin = 6;
constexpr int kTouchActiveLevel = HIGH;

// Optional high-side switch for the sensor's 3V3 rail. -1 means permanently on.
constexpr int8_t kSensorPowerPin = 7;

// Held at boot, or held for kEnrollHoldMs while running, to start enrolment.
constexpr int8_t kButtonPin = 0;  // BOOT button
constexpr int kButtonActiveLevel = LOW;

// On-board addressable LED. -1 disables all LED feedback.
constexpr int8_t kStatusLedPin = 48;

// --------------------------------------------------------------------------- //
// Sensor
// --------------------------------------------------------------------------- //

constexpr uint32_t kSensorBaud = 57600;
constexpr uint32_t kSensorPassword = 0x00000000;  // factory default
constexpr uint16_t kTemplateCapacity = 50;

// How long a single enrolment step waits for a finger to arrive or leave.
constexpr uint32_t kEnrollStepTimeoutMs = 15000;
constexpr uint8_t kEnrollCaptures = 4;

// Matches below this score are treated as misses regardless of what the module
// says. The module's own threshold is permissive by default.
constexpr uint16_t kMinMatchScore = 60;

// --------------------------------------------------------------------------- //
// Behaviour
// --------------------------------------------------------------------------- //

// After a successful unlock, ignore the sensor for this long. Without it, a
// finger left resting on the platen types the secret several times over.
constexpr uint32_t kUnlockCooldownMs = 4000;

// Consecutive failed attempts before the device stops trying. Clears on a
// successful match or on a power cycle. This is a brute-force brake, not a
// security boundary -- see docs/security.md.
constexpr uint8_t kMaxConsecutiveFailures = 8;
constexpr uint32_t kLockoutMs = 30000;

// How long the button must be held to enter enrolment.
constexpr uint32_t kEnrollHoldMs = 2000;

// Refuse to store a secret unless the chip's flash encryption is enabled.
//
// Without flash encryption, NVS is plaintext: two minutes with esptool reads
// the secret straight back off the board. Turning this on makes the device
// useless until you have burned the eFuse, which is the point -- it is the
// difference between "a secret on a device" and "a secret written on the
// outside of the device". Off by default only so a bench prototype works
// before you commit to a one-way eFuse operation. See docs/security.md.
constexpr bool kRequireFlashEncryption = false;

// Require a button press after a successful match before anything is typed.
// Off by default because it doubles the effort of every unlock; turn it on if
// the device travels between machines, where an accidental touch would type the
// secret into whatever window happens to be focused.
constexpr bool kConfirmWithButton = false;
constexpr uint32_t kConfirmTimeoutMs = 5000;

// Keystroke pacing. macOS drops characters from a HID device that types faster
// than the login window redraws, so the gaps are deliberate.
constexpr uint32_t kWakeToTypeDelayMs = 450;
constexpr uint32_t kKeystrokeDelayMs = 12;
constexpr uint32_t kTypeToEnterDelayMs = 90;

// USB descriptor strings. Change these if you would rather the device not
// announce what it is on every machine it touches.
constexpr char kUsbManufacturer[] = "Standalone Fingerprint Key";
constexpr char kUsbProduct[] = "Fingerprint Key";
constexpr uint16_t kUsbVendorId = 0x303A;   // Espressif
constexpr uint16_t kUsbProductId = 0x8000;

}  // namespace fpkey
