#include "status_led.h"

#include "config.h"

namespace fpkey {
namespace {

constexpr uint32_t kFrameIntervalMs = 25;  // 40 fps is plenty for a breath
constexpr uint8_t kMaxBrightness = 40;     // the on-board LED is painfully bright

// Triangle wave in [0, 255] with the given period.
uint8_t Breath(uint32_t elapsed_ms, uint32_t period_ms) {
  const uint32_t phase = elapsed_ms % period_ms;
  const uint32_t half = period_ms / 2;
  const uint32_t up = phase < half ? phase : period_ms - phase;
  return static_cast<uint8_t>((up * 255) / half);
}

uint8_t Scale(uint8_t value, uint8_t level) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * level) / 255);
}

}  // namespace

void StatusLed::Begin() {
  if (kStatusLedPin < 0) return;
  entered_at_ = millis();
  Emit(0, 0, 0);
}

void StatusLed::Set(LedState state) {
  if (state == state_) return;
  state_ = state;
  entered_at_ = millis();
  // Paint it now rather than waiting for the next Tick(). Enrolment blocks for
  // the better part of a minute without reaching the main loop, and the LED
  // would otherwise sit on the previous colour for all of it.
  last_emit_ = 0;
  Tick();
}

void StatusLed::Tick() {
  if (kStatusLedPin < 0) return;

  const uint32_t now = millis();
  if (now - last_emit_ < kFrameIntervalMs) return;
  last_emit_ = now;

  const uint32_t elapsed = now - entered_at_;

  switch (state_) {
    case LedState::kBooting:
      Emit(kMaxBrightness, Scale(140, kMaxBrightness), 0);
      break;

    case LedState::kIdle: {
      const uint8_t level = Scale(Breath(elapsed, 4000), kMaxBrightness / 3);
      Emit(0, 0, level);
      break;
    }

    case LedState::kScanning:
      Emit(kMaxBrightness, kMaxBrightness, kMaxBrightness);
      break;

    case LedState::kSuccess:
      Emit(0, kMaxBrightness, 0);
      break;

    case LedState::kFailure:
      Emit(kMaxBrightness, 0, 0);
      break;

    case LedState::kEnrolling: {
      const uint8_t level = Scale(Breath(elapsed, 900), kMaxBrightness);
      Emit(level, Scale(level, 140), 0);
      break;
    }

    case LedState::kLockedOut:
      Emit((elapsed / 700) % 2 ? kMaxBrightness : 0, 0, 0);
      break;

    case LedState::kNoSecret: {
      const uint8_t level = Scale(Breath(elapsed, 2000), kMaxBrightness);
      Emit(level, 0, level);
      break;
    }
  }
}

void StatusLed::Emit(uint8_t r, uint8_t g, uint8_t b) {
  if (r == last_rgb_[0] && g == last_rgb_[1] && b == last_rgb_[2]) return;
  last_rgb_[0] = r;
  last_rgb_[1] = g;
  last_rgb_[2] = b;
  neopixelWrite(kStatusLedPin, r, g, b);
}

}  // namespace fpkey
