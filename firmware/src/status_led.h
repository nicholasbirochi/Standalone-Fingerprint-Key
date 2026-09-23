// Non-blocking feedback on the board's addressable LED.
//
// Call Set() to change what the device is saying and Tick() from the main loop
// to animate it. Nothing here blocks, because the loop also has to stay
// responsive to the sensor.

#pragma once

#include <Arduino.h>

namespace fpkey {

enum class LedState {
  kBooting,    // amber, solid
  kIdle,       // slow blue breath
  kScanning,   // white, solid
  kSuccess,    // green, brief
  kFailure,    // red, brief
  kEnrolling,  // amber pulse
  kLockedOut,  // red, slow blink
  kNoSecret,   // magenta breath: armed but nothing to type
};

class StatusLed {
 public:
  void Begin();
  void Set(LedState state);
  void Tick();

  LedState state() const { return state_; }

 private:
  void Emit(uint8_t r, uint8_t g, uint8_t b);

  LedState state_ = LedState::kBooting;
  uint32_t entered_at_ = 0;
  uint32_t last_emit_ = 0;
  uint8_t last_rgb_[3] = {255, 255, 255};  // forces the first Emit to write
};

}  // namespace fpkey
