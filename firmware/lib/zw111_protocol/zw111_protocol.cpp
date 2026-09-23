#include "zw111_protocol.h"

namespace zw111 {
namespace {

void PutU32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value >> 24);
  out[1] = static_cast<uint8_t>(value >> 16);
  out[2] = static_cast<uint8_t>(value >> 8);
  out[3] = static_cast<uint8_t>(value);
}

}  // namespace

uint16_t Checksum(PacketId pid, const uint8_t* payload, size_t length) {
  // The length field covers the payload plus the two checksum bytes, and is
  // itself part of the sum.
  const uint16_t declared = static_cast<uint16_t>(length + 2);
  uint16_t sum = static_cast<uint8_t>(pid);
  sum = static_cast<uint16_t>(sum + (declared >> 8));
  sum = static_cast<uint16_t>(sum + (declared & 0xFF));
  for (size_t i = 0; i < length; ++i) {
    sum = static_cast<uint16_t>(sum + payload[i]);
  }
  return sum;
}

size_t Encode(uint8_t* out, size_t capacity, uint32_t address, PacketId pid,
              const uint8_t* payload, size_t length) {
  if (length > kMaxPayload) return 0;
  const size_t total = length + kFrameOverhead;
  if (capacity < total) return 0;

  out[0] = kHeaderHigh;
  out[1] = kHeaderLow;
  PutU32(out + 2, address);
  out[6] = static_cast<uint8_t>(pid);

  const uint16_t declared = static_cast<uint16_t>(length + 2);
  out[7] = static_cast<uint8_t>(declared >> 8);
  out[8] = static_cast<uint8_t>(declared & 0xFF);
  for (size_t i = 0; i < length; ++i) {
    out[9 + i] = payload[i];
  }

  const uint16_t sum = Checksum(pid, payload, length);
  out[9 + length] = static_cast<uint8_t>(sum >> 8);
  out[10 + length] = static_cast<uint8_t>(sum & 0xFF);
  return total;
}

size_t EncodeCommand(uint8_t* out, size_t capacity, uint32_t address, Command command,
                     const uint8_t* args, size_t args_length) {
  if (args_length + 1 > kMaxPayload) return 0;
  uint8_t payload[kMaxPayload];
  payload[0] = static_cast<uint8_t>(command);
  for (size_t i = 0; i < args_length; ++i) {
    payload[1 + i] = args[i];
  }
  return Encode(out, capacity, address, PacketId::kCommand, payload, args_length + 1);
}

void Parser::Reset() {
  state_ = State::kHeaderHigh;
  address_ = 0;
  pid_ = 0;
  declared_length_ = 0;
  payload_length_ = 0;
  received_ = 0;
  checksum_ = 0;
}

Parser::Result Parser::Feed(uint8_t byte) {
  switch (state_) {
    case State::kHeaderHigh:
      if (byte == kHeaderHigh) state_ = State::kHeaderLow;
      return Result::kNeedMore;

    case State::kHeaderLow:
      // A second 0xEF is still a plausible start of header, so hold position
      // rather than throwing the byte away.
      if (byte == kHeaderLow) {
        state_ = State::kAddress;
        address_ = 0;
        received_ = 0;
      } else if (byte != kHeaderHigh) {
        state_ = State::kHeaderHigh;
      }
      return Result::kNeedMore;

    case State::kAddress:
      address_ = (address_ << 8) | byte;
      if (++received_ == 4) state_ = State::kPid;
      return Result::kNeedMore;

    case State::kPid:
      pid_ = byte;
      state_ = State::kLengthHigh;
      return Result::kNeedMore;

    case State::kLengthHigh:
      declared_length_ = static_cast<uint16_t>(byte << 8);
      state_ = State::kLengthLow;
      return Result::kNeedMore;

    case State::kLengthLow: {
      declared_length_ = static_cast<uint16_t>(declared_length_ | byte);
      // The declared length includes the two checksum bytes, so anything below
      // 2 is malformed and anything above our buffer we refuse outright.
      if (declared_length_ < 2 || declared_length_ - 2 > static_cast<int>(kMaxPayload)) {
        Reset();
        return Result::kOverflow;
      }
      payload_length_ = static_cast<size_t>(declared_length_ - 2);
      received_ = 0;
      checksum_ = 0;
      state_ = payload_length_ == 0 ? State::kChecksumHigh : State::kPayload;
      return Result::kNeedMore;
    }

    case State::kPayload:
      buffer_[received_++] = byte;
      if (received_ == payload_length_) state_ = State::kChecksumHigh;
      return Result::kNeedMore;

    case State::kChecksumHigh:
      checksum_ = static_cast<uint16_t>(byte << 8);
      state_ = State::kChecksumLow;
      return Result::kNeedMore;

    case State::kChecksumLow: {
      const uint16_t received_sum = static_cast<uint16_t>(checksum_ | byte);
      const uint16_t expected =
          Checksum(static_cast<PacketId>(pid_), buffer_, payload_length_);
      const uint32_t address = address_;
      const uint8_t pid = pid_;
      const size_t length = payload_length_;
      Reset();
      if (received_sum != expected) return Result::kBadChecksum;

      frame_.address = address;
      frame_.pid = static_cast<PacketId>(pid);
      frame_.payload = buffer_;
      frame_.length = length;
      return Result::kFrame;
    }
  }
  Reset();
  return Result::kNeedMore;
}

const char* StatusName(Status status) {
  switch (status) {
    case Status::kOk: return "ok";
    case Status::kPacketError: return "packet error";
    case Status::kNoFinger: return "no finger";
    case Status::kImageFail: return "image capture failed";
    case Status::kImageMessy: return "image too noisy";
    case Status::kTooFewPoints: return "too few feature points";
    case Status::kNoMatch: return "no match";
    case Status::kNotFound: return "not found in library";
    case Status::kMergeFail: return "could not merge captures";
    case Status::kOutOfRange: return "page id out of range";
    case Status::kReadTemplateFail: return "template read failed";
    case Status::kDeleteFail: return "delete failed";
    case Status::kClearFail: return "clear library failed";
    case Status::kWrongPassword: return "wrong module password";
    case Status::kNoValidImage: return "no valid image in buffer";
    case Status::kFlashError: return "flash error";
    case Status::kTimeout: return "module timeout";
    case Status::kDuplicate: return "finger already enrolled";
  }
  return "unknown";
}

}  // namespace zw111
