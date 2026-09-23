// Line-based admin console over the USB CDC port.
//
// This is how a finger gets enrolled and how the secret gets set. It is
// deliberately write-only where the secret is concerned: there is no command
// that prints it back, because the same cable that carries this console is
// plugged into whatever machine happens to be nearby.

#pragma once

#include <Arduino.h>

#include "hid_typer.h"
#include "secret_store.h"
#include "sensor.h"
#include "status_led.h"

namespace fpkey {

class Console {
 public:
  Console(Sensor& sensor, SecretStore& store, HidTyper& typer, StatusLed& led)
      : sensor_(sensor), store_(store), typer_(typer), led_(led) {}

  void Begin();

  // Reads whatever is waiting on the port and runs any complete line. Commands
  // that take time (enrolment) block here, so the caller gets the loop back
  // only once the device is idle again. Returns true if a command ran, so the
  // main loop can skip a sensor poll and stay responsive.
  bool Poll();

 private:
  void Dispatch(char* line);
  void PrintHelp();
  void PrintStatus();
  void RunEnroll(char* argument);
  void Prompt();

  Sensor& sensor_;
  SecretStore& store_;
  HidTyper& typer_;
  StatusLed& led_;

  char buffer_[160] = {};
  size_t length_ = 0;
};

}  // namespace fpkey
