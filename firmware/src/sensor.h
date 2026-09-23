// Arduino-side driver for the HLK-ZW111 over UART.
//
// The framing lives in lib/zw111_protocol so it can be tested on a host; this
// file is only the transport and the command sequences.

#pragma once

#include <Arduino.h>

#include "zw111_protocol.h"

namespace fpkey {

// Result of a single identification attempt.
struct MatchResult {
  bool matched = false;
  uint16_t page_id = 0;   // slot in the module's template library
  uint16_t score = 0;     // higher is a closer match
  zw111::Status status = zw111::Status::kNoFinger;
};

// Progress callback for enrolment, so the UI layer can prompt for each press.
using EnrollProgress = void (*)(uint8_t capture, uint8_t of_total, const char* message);

class Sensor {
 public:
  Sensor(HardwareSerial& uart, int8_t rx_pin, int8_t tx_pin, int8_t touch_pin);

  // Brings up the UART and checks that the module answers. Returns false if it
  // never acknowledges, which in practice means wiring or baud rate.
  bool Begin(uint32_t baud, uint32_t password);

  // True while a finger is resting on the sensor, read from the module's touch
  // output. Polling the image sensor instead would work but keeps it awake.
  bool FingerPresent() const;

  // One capture-and-search cycle. Returns quickly with status kNoFinger when
  // there is nothing to read, so this is safe to call from the main loop.
  MatchResult Identify();

  // Captures `captures` impressions of the same finger and stores the merged
  // template at `page_id`. Blocking, and intended to be driven by the console.
  bool Enroll(uint16_t page_id, uint8_t captures, EnrollProgress progress);

  bool DeleteTemplate(uint16_t page_id);
  bool ClearLibrary();

  // Number of templates currently stored, or -1 if the module did not answer.
  int TemplateCount();

  // Ring LED. `colour` follows the module's own encoding; see docs/protocol.md.
  bool SetAuraLed(uint8_t control, uint8_t speed, uint8_t colour, uint8_t cycles);

  // Puts the module into its low-power state. It wakes on touch.
  bool Sleep();

  zw111::Status last_status() const { return last_status_; }

 private:
  // Sends a command and waits for the matching ack. `ack_payload` receives the
  // payload bytes after the confirmation code.
  bool Transact(zw111::Command command, const uint8_t* args, size_t args_length,
                uint8_t* ack_payload, size_t ack_capacity, size_t* ack_length,
                uint32_t timeout_ms);

  bool ReadFrame(uint32_t timeout_ms);

  HardwareSerial& uart_;
  int8_t rx_pin_;
  int8_t tx_pin_;
  int8_t touch_pin_;
  uint32_t address_ = zw111::kDefaultAddress;
  zw111::Parser parser_;
  zw111::Status last_status_ = zw111::Status::kOk;
};

}  // namespace fpkey
