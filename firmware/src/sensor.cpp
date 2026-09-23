#include "sensor.h"

#include "config.h"

namespace fpkey {
namespace {

constexpr uint32_t kDefaultTimeoutMs = 500;
constexpr uint32_t kCaptureTimeoutMs = 2000;
constexpr uint32_t kFlashTimeoutMs = 5000;  // stores and erases hit flash

void PutU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t GetU16(const uint8_t* in) {
  return static_cast<uint16_t>((in[0] << 8) | in[1]);
}

}  // namespace

Sensor::Sensor(HardwareSerial& uart, int8_t rx_pin, int8_t tx_pin, int8_t touch_pin)
    : uart_(uart), rx_pin_(rx_pin), tx_pin_(tx_pin), touch_pin_(touch_pin) {}

bool Sensor::Begin(uint32_t baud, uint32_t password) {
  uart_.begin(baud, SERIAL_8N1, rx_pin_, tx_pin_);
  if (touch_pin_ >= 0) pinMode(touch_pin_, INPUT);

  // The module needs a moment after power-up before it will answer.
  delay(200);
  while (uart_.available()) uart_.read();

  uint8_t args[4];
  args[0] = static_cast<uint8_t>(password >> 24);
  args[1] = static_cast<uint8_t>(password >> 16);
  args[2] = static_cast<uint8_t>(password >> 8);
  args[3] = static_cast<uint8_t>(password);

  // Retry: the first command after power-up is routinely lost while the module
  // finishes its own boot.
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (Transact(zw111::Command::kVerifyPassword, args, sizeof(args), nullptr, 0, nullptr,
                 kDefaultTimeoutMs)) {
      return true;
    }
    delay(150);
  }
  return false;
}

bool Sensor::FingerPresent() const {
  if (touch_pin_ < 0) return true;  // no touch line wired: caller must poll
  return digitalRead(touch_pin_) == kTouchActiveLevel;
}

MatchResult Sensor::Identify() {
  MatchResult result;

  if (!Transact(zw111::Command::kGetImage, nullptr, 0, nullptr, 0, nullptr,
                kCaptureTimeoutMs)) {
    result.status = last_status_;
    return result;
  }

  const uint8_t gen_args[] = {0x01};  // feature buffer 1
  if (!Transact(zw111::Command::kGenChar, gen_args, sizeof(gen_args), nullptr, 0, nullptr,
                kCaptureTimeoutMs)) {
    result.status = last_status_;
    return result;
  }

  // Search the whole library starting at page 0.
  uint8_t search_args[5];
  search_args[0] = 0x01;  // buffer 1
  PutU16(search_args + 1, 0);
  PutU16(search_args + 3, kTemplateCapacity);

  uint8_t ack[8];
  size_t ack_length = 0;
  if (!Transact(zw111::Command::kSearch, search_args, sizeof(search_args), ack, sizeof(ack),
                &ack_length, kCaptureTimeoutMs)) {
    result.status = last_status_;
    return result;
  }
  if (ack_length < 4) {
    result.status = zw111::Status::kPacketError;
    return result;
  }

  result.matched = true;
  result.page_id = GetU16(ack);
  result.score = GetU16(ack + 2);
  result.status = zw111::Status::kOk;
  return result;
}

bool Sensor::Enroll(uint16_t page_id, uint8_t captures, EnrollProgress progress) {
  if (captures < 1) captures = 1;
  if (captures > 6) captures = 6;  // the module has six feature buffers

  for (uint8_t i = 0; i < captures; ++i) {
    if (progress) progress(i + 1, captures, "place finger");

    // Wait for a finger, then capture. GetImage answers kNoFinger immediately
    // when the platen is empty, so this polls rather than blocking the module.
    const uint32_t deadline = millis() + kEnrollStepTimeoutMs;
    bool captured = false;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
      if (Transact(zw111::Command::kGetImage, nullptr, 0, nullptr, 0, nullptr,
                   kCaptureTimeoutMs)) {
        captured = true;
        break;
      }
      if (last_status_ != zw111::Status::kNoFinger) {
        if (progress) progress(i + 1, captures, zw111::StatusName(last_status_));
      }
      delay(60);
    }
    if (!captured) {
      last_status_ = zw111::Status::kTimeout;
      return false;
    }

    const uint8_t gen_args[] = {static_cast<uint8_t>(i + 1)};
    if (!Transact(zw111::Command::kGenChar, gen_args, sizeof(gen_args), nullptr, 0, nullptr,
                  kCaptureTimeoutMs)) {
      return false;
    }

    if (progress) progress(i + 1, captures, "captured, lift finger");
    // Wait for the finger to come off, otherwise the next GetImage returns the
    // same impression and the merged template is no better than one capture.
    const uint32_t lift_deadline = millis() + kEnrollStepTimeoutMs;
    while (static_cast<int32_t>(lift_deadline - millis()) > 0) {
      if (!Transact(zw111::Command::kGetImage, nullptr, 0, nullptr, 0, nullptr,
                    kCaptureTimeoutMs) &&
          last_status_ == zw111::Status::kNoFinger) {
        break;
      }
      delay(60);
    }
  }

  if (!Transact(zw111::Command::kRegModel, nullptr, 0, nullptr, 0, nullptr,
                kCaptureTimeoutMs)) {
    return false;
  }

  uint8_t store_args[3];
  store_args[0] = 0x01;  // merged template lands in buffer 1
  PutU16(store_args + 1, page_id);
  return Transact(zw111::Command::kStoreChar, store_args, sizeof(store_args), nullptr, 0,
                  nullptr, kFlashTimeoutMs);
}

bool Sensor::DeleteTemplate(uint16_t page_id) {
  uint8_t args[4];
  PutU16(args, page_id);
  PutU16(args + 2, 1);  // delete one entry
  return Transact(zw111::Command::kDeleteChar, args, sizeof(args), nullptr, 0, nullptr,
                  kFlashTimeoutMs);
}

bool Sensor::ClearLibrary() {
  return Transact(zw111::Command::kEmpty, nullptr, 0, nullptr, 0, nullptr, kFlashTimeoutMs);
}

int Sensor::TemplateCount() {
  uint8_t ack[4];
  size_t ack_length = 0;
  if (!Transact(zw111::Command::kTemplateNum, nullptr, 0, ack, sizeof(ack), &ack_length,
                kDefaultTimeoutMs)) {
    return -1;
  }
  if (ack_length < 2) return -1;
  return GetU16(ack);
}

bool Sensor::SetAuraLed(uint8_t control, uint8_t speed, uint8_t colour, uint8_t cycles) {
  const uint8_t args[] = {control, speed, colour, cycles};
  return Transact(zw111::Command::kAuraLedConfig, args, sizeof(args), nullptr, 0, nullptr,
                  kDefaultTimeoutMs);
}

bool Sensor::Sleep() {
  return Transact(zw111::Command::kSleep, nullptr, 0, nullptr, 0, nullptr, kDefaultTimeoutMs);
}

// --------------------------------------------------------------------------- //
// Transport
// --------------------------------------------------------------------------- //

bool Sensor::ReadFrame(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  parser_.Reset();
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    while (uart_.available()) {
      const zw111::Parser::Result result = parser_.Feed(static_cast<uint8_t>(uart_.read()));
      if (result == zw111::Parser::Result::kFrame) return true;
      // Bad checksums and oversized frames are dropped; the parser has already
      // reset itself, so keep reading until the deadline.
    }
    delay(1);
  }
  return false;
}

bool Sensor::Transact(zw111::Command command, const uint8_t* args, size_t args_length,
                      uint8_t* ack_payload, size_t ack_capacity, size_t* ack_length,
                      uint32_t timeout_ms) {
  uint8_t frame[zw111::kMaxFrame];
  const size_t n =
      zw111::EncodeCommand(frame, sizeof(frame), address_, command, args, args_length);
  if (n == 0) {
    last_status_ = zw111::Status::kPacketError;
    return false;
  }

  while (uart_.available()) uart_.read();  // drop anything stale
  uart_.write(frame, n);
  uart_.flush();

  if (!ReadFrame(timeout_ms)) {
    last_status_ = zw111::Status::kTimeout;
    return false;
  }

  const zw111::Frame& ack = parser_.frame();
  if (ack.pid != zw111::PacketId::kAck || ack.length < 1) {
    last_status_ = zw111::Status::kPacketError;
    return false;
  }

  last_status_ = ack.status();
  if (ack_payload != nullptr && ack_length != nullptr) {
    const size_t extra = ack.length - 1;
    const size_t copied = extra < ack_capacity ? extra : ack_capacity;
    for (size_t i = 0; i < copied; ++i) ack_payload[i] = ack.payload[1 + i];
    *ack_length = copied;
  }
  return last_status_ == zw111::Status::kOk;
}

}  // namespace fpkey
