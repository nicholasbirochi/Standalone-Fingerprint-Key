// Packet framing for the HLK-ZW111 fingerprint module.
//
// The ZW111 speaks the 0xEF01 packet protocol shared by the ZhianTec / Synochip
// family (R30x, ZW101, ZW111). This header deliberately knows nothing about
// Arduino or about a serial port: it is pure byte pushing, so the framing can be
// unit tested on a host machine where a wrong checksum is cheap to find.
//
//   header    2 bytes   0xEF 0x01
//   address   4 bytes   big endian, 0xFFFFFFFF until changed
//   pid       1 byte    command / data / ack / end-of-data
//   length    2 bytes   big endian, payload + 2 checksum bytes
//   payload   n bytes
//   checksum  2 bytes   big endian sum of pid, length and payload

#ifndef ZW111_PROTOCOL_H
#define ZW111_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace zw111 {

constexpr uint8_t kHeaderHigh = 0xEF;
constexpr uint8_t kHeaderLow = 0x01;
constexpr uint32_t kDefaultAddress = 0xFFFFFFFFu;

// Longest payload we are willing to buffer. Commands and acks are tiny; the
// only large transfers are template up/downloads, which this firmware does not
// use, so a small buffer keeps the parser bounded.
constexpr size_t kMaxPayload = 64;
constexpr size_t kFrameOverhead = 11;  // 2 header + 4 address + 1 pid + 2 length + 2 checksum
constexpr size_t kMaxFrame = kMaxPayload + kFrameOverhead;

enum class PacketId : uint8_t {
  kCommand = 0x01,
  kData = 0x02,
  kAck = 0x07,
  kEndOfData = 0x08,
};

// Instruction codes. Only the ones this firmware issues are listed; the module
// supports considerably more.
enum class Command : uint8_t {
  kGetImage = 0x01,
  kGenChar = 0x02,
  kMatch = 0x03,
  kSearch = 0x04,
  kRegModel = 0x05,
  kStoreChar = 0x06,
  kDeleteChar = 0x0C,
  kEmpty = 0x0D,
  kReadSysPara = 0x0F,
  kVerifyPassword = 0x13,
  kTemplateNum = 0x1D,
  kCancel = 0x30,
  kAutoEnroll = 0x31,
  kAutoIdentify = 0x32,
  kSleep = 0x33,
  kAuraLedConfig = 0x35,
};

// Confirmation codes, returned as the first payload byte of every ack.
enum class Status : uint8_t {
  kOk = 0x00,
  kPacketError = 0x01,
  kNoFinger = 0x02,
  kImageFail = 0x03,
  kImageMessy = 0x06,
  kTooFewPoints = 0x07,
  kNoMatch = 0x08,
  kNotFound = 0x09,
  kMergeFail = 0x0A,
  kOutOfRange = 0x0B,
  kReadTemplateFail = 0x0C,
  kDeleteFail = 0x10,
  kClearFail = 0x11,
  kWrongPassword = 0x13,
  kNoValidImage = 0x15,
  kFlashError = 0x18,
  kTimeout = 0x26,
  kDuplicate = 0x27,
};

// A decoded frame. `payload` points into the parser's own buffer and stays valid
// only until the next byte is fed in.
struct Frame {
  uint32_t address;
  PacketId pid;
  const uint8_t* payload;
  size_t length;

  // Every ack starts with a confirmation code; anything shorter is malformed.
  bool ok() const {
    return pid == PacketId::kAck && length >= 1 && payload[0] == static_cast<uint8_t>(Status::kOk);
  }
  Status status() const {
    return length >= 1 ? static_cast<Status>(payload[0]) : Status::kPacketError;
  }
};

// Sum of pid, both length bytes and the payload, truncated to 16 bits.
uint16_t Checksum(PacketId pid, const uint8_t* payload, size_t length);

// Serialise one frame into `out`. Returns the number of bytes written, or 0 if
// the payload is too long or the buffer too small.
size_t Encode(uint8_t* out, size_t capacity, uint32_t address, PacketId pid,
              const uint8_t* payload, size_t length);

// Convenience wrapper for the common case of a command with fixed arguments.
size_t EncodeCommand(uint8_t* out, size_t capacity, uint32_t address, Command command,
                     const uint8_t* args, size_t args_length);

// Incremental parser. Feed it one byte at a time; it resynchronises on its own
// when it sees garbage, which matters on a UART that may have been powered up
// mid-transmission.
class Parser {
 public:
  enum class Result {
    kNeedMore,     // nothing to report yet
    kFrame,        // a complete, checksum-valid frame is available
    kBadChecksum,  // a frame arrived intact but corrupted; parser has reset
    kOverflow,     // the frame claimed more payload than we will buffer
  };

  void Reset();

  // Returns kFrame when `frame()` has been filled in by this byte.
  Result Feed(uint8_t byte);

  const Frame& frame() const { return frame_; }

 private:
  enum class State : uint8_t {
    kHeaderHigh,
    kHeaderLow,
    kAddress,
    kPid,
    kLengthHigh,
    kLengthLow,
    kPayload,
    kChecksumHigh,
    kChecksumLow,
  };

  State state_ = State::kHeaderHigh;
  uint32_t address_ = 0;
  uint8_t pid_ = 0;
  uint16_t declared_length_ = 0;  // payload + 2 checksum bytes, as sent
  size_t payload_length_ = 0;
  size_t received_ = 0;
  uint16_t checksum_ = 0;
  uint8_t buffer_[kMaxPayload] = {};
  Frame frame_ = {};
};

// Human-readable name for a confirmation code, for logs and the serial console.
const char* StatusName(Status status);

}  // namespace zw111

#endif  // ZW111_PROTOCOL_H
