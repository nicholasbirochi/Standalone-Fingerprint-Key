// Standalone-Fingerprint-Key
//
// Reads a finger on an HLK-ZW111, and on a match types a stored secret over USB
// HID. The host sees a keyboard and nothing else, which is why this works on a
// Mac with no driver, no daemon, and no permission to install either.
//
// The whole device is one loop: animate the LED, service the console, and if
// neither of those is busy, look for a finger.

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "console.h"
#include "hid_typer.h"
#include "secret_store.h"
#include "sensor.h"
#include "status_led.h"

namespace {

using namespace fpkey;

Sensor g_sensor(Serial1, kSensorRxPin, kSensorTxPin, kSensorTouchPin);
SecretStore g_store;
HidTyper g_typer;
StatusLed g_led;
Console g_console(g_sensor, g_store, g_typer, g_led);

bool g_sensor_ready = false;
uint8_t g_failures = 0;
uint32_t g_cooldown_until = 0;
uint32_t g_lockout_until = 0;
uint32_t g_button_down_at = 0;

bool Elapsed(uint32_t deadline) { return static_cast<int32_t>(deadline - millis()) <= 0; }

bool ButtonDown() {
  if (kButtonPin < 0) return false;
  return digitalRead(kButtonPin) == kButtonActiveLevel;
}

void PowerUpSensor() {
  if (kSensorPowerPin < 0) return;
  pinMode(kSensorPowerPin, OUTPUT);
  digitalWrite(kSensorPowerPin, HIGH);
  delay(50);
}

// Types the stored secret. Split out because both the sensor path and a future
// button path want exactly this sequence and its ordering matters: wake first,
// give the login window time to draw, only then type.
void Unlock() {
  // An unenumerated host silently swallows every keystroke, so check before
  // claiming success: a green flash with nothing typed is worse than a red one.
  if (!g_typer.Ready()) {
    g_led.Set(LedState::kFailure);
    return;
  }

  char secret[kMaxSecretLength + 1];
  if (!g_store.Read(secret, sizeof(secret))) {
    g_led.Set(LedState::kNoSecret);
    return;
  }

  g_typer.WakeHost();
  delay(kWakeToTypeDelayMs);
  g_typer.TypeSecret(secret);
  memset(secret, 0, sizeof(secret));

  g_led.Set(LedState::kSuccess);
  g_cooldown_until = millis() + kUnlockCooldownMs;
}

// Waits for a deliberate button press before typing. Only reached when
// kConfirmWithButton is on.
bool WaitForConfirmation() {
  const uint32_t deadline = millis() + kConfirmTimeoutMs;
  while (!Elapsed(deadline)) {
    g_led.Tick();
    if (ButtonDown()) {
      while (ButtonDown()) delay(10);  // debounce on release
      return true;
    }
    delay(10);
  }
  return false;
}

void HandleMatch(const MatchResult& match) {
  const uint16_t allowed = g_store.allowed_template();
  const bool slot_ok = allowed == SecretStore::kAnyTemplate || allowed == match.page_id;

  if (!slot_ok || match.score < kMinMatchScore) {
    g_led.Set(LedState::kFailure);
    ++g_failures;
    return;
  }

  g_failures = 0;
  if (kConfirmWithButton) {
    g_led.Set(LedState::kSuccess);
    if (!WaitForConfirmation()) {
      g_led.Set(LedState::kIdle);
      return;
    }
  }
  Unlock();
}

void PollSensor() {
  if (!g_sensor_ready) return;
  if (!Elapsed(g_cooldown_until)) return;
  if (!g_sensor.FingerPresent()) {
    if (g_led.state() == LedState::kScanning) g_led.Set(LedState::kIdle);
    return;
  }

  g_led.Set(LedState::kScanning);
  const MatchResult match = g_sensor.Identify();

  if (match.matched) {
    HandleMatch(match);
    return;
  }

  switch (match.status) {
    case zw111::Status::kNoFinger:
      // The touch line fired but the image sensor saw nothing: a brush past,
      // not an attempt. Not a failure.
      g_led.Set(LedState::kIdle);
      break;
    case zw111::Status::kNotFound:
    case zw111::Status::kNoMatch:
      g_led.Set(LedState::kFailure);
      ++g_failures;
      break;
    default:
      // Bad captures (smudges, partial presses) are common and should not count
      // toward the brute-force brake.
      g_led.Set(LedState::kFailure);
      break;
  }
  delay(400);  // let the user see the result before the LED returns to idle
}

// Hold the button while running to start enrolment without a serial console.
void PollButton() {
  if (kButtonPin < 0) return;

  if (!ButtonDown()) {
    g_button_down_at = 0;
    return;
  }
  if (g_button_down_at == 0) {
    g_button_down_at = millis();
    return;
  }
  if (millis() - g_button_down_at < kEnrollHoldMs) return;

  g_button_down_at = 0;
  while (ButtonDown()) delay(10);

  const int count = g_sensor.TemplateCount();
  if (count < 0 || count >= kTemplateCapacity) {
    g_led.Set(LedState::kFailure);
    return;
  }
  g_led.Set(LedState::kEnrolling);
  Serial.printf("button enrolment into slot %d\n", count);
  const bool ok =
      g_sensor.Enroll(static_cast<uint16_t>(count), kEnrollCaptures, nullptr);
  g_led.Set(ok ? LedState::kSuccess : LedState::kFailure);
  delay(800);
}

}  // namespace

void setup() {
  g_led.Begin();
  g_led.Set(LedState::kBooting);

  if (kButtonPin >= 0) pinMode(kButtonPin, kButtonActiveLevel == LOW ? INPUT_PULLUP : INPUT);

  g_typer.Begin();
  g_console.Begin();
  g_store.Begin();

  PowerUpSensor();
  g_sensor_ready = g_sensor.Begin(kSensorBaud, kSensorPassword);

  Serial.println();
  Serial.println("Standalone-Fingerprint-Key");
  if (!g_sensor_ready) {
    Serial.println("sensor did not answer -- check wiring, baud rate and module password");
  }
  Serial.println("type 'help' for commands");
  Serial.print("fpkey> ");

  g_led.Set(g_store.HasSecret() ? LedState::kIdle : LedState::kNoSecret);
}

void loop() {
  g_led.Tick();

  // A console command may have taken seconds (enrolment); skip this pass so
  // the sensor is not polled with a finger still on it from that flow.
  if (g_console.Poll()) return;

  if (g_failures >= kMaxConsecutiveFailures) {
    if (g_lockout_until == 0) {
      g_lockout_until = millis() + kLockoutMs;
      g_led.Set(LedState::kLockedOut);
      Serial.println("too many failed attempts; pausing");
    }
    if (!Elapsed(g_lockout_until)) return;
    g_lockout_until = 0;
    g_failures = 0;
    g_led.Set(LedState::kIdle);
  }

  PollButton();
  PollSensor();

  // Settle back to a resting state once a success or failure flash has been on
  // screen long enough to read.
  const LedState state = g_led.state();
  if (state == LedState::kSuccess || state == LedState::kFailure) {
    if (Elapsed(g_cooldown_until)) {
      g_led.Set(g_store.HasSecret() ? LedState::kIdle : LedState::kNoSecret);
    }
  }
}
