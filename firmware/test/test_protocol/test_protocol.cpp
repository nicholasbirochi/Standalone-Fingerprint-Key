// Host-side tests for the ZW111 packet framing.
//
//   pio test -e native
//
// These run without a board attached. Everything here is about bytes on the
// wire: a wrong checksum or an off-by-one in the length field produces a module
// that silently ignores commands, which is miserable to debug against hardware.

#include <unity.h>

#include <cstring>

#include "zw111_protocol.h"

using zw111::Command;
using zw111::Frame;
using zw111::PacketId;
using zw111::Parser;
using zw111::Status;

namespace {

// Feed a whole buffer through the parser and return the last result.
Parser::Result FeedAll(Parser& parser, const uint8_t* data, size_t length) {
  Parser::Result result = Parser::Result::kNeedMore;
  for (size_t i = 0; i < length; ++i) {
    result = parser.Feed(data[i]);
  }
  return result;
}

}  // namespace

void setUp() {}
void tearDown() {}

// The canonical example from the protocol datasheet: a bare GetImage command.
void test_encode_get_image_matches_datasheet() {
  uint8_t out[zw111::kMaxFrame];
  const size_t n = zw111::EncodeCommand(out, sizeof(out), zw111::kDefaultAddress,
                                        Command::kGetImage, nullptr, 0);
  const uint8_t expected[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                              0x01, 0x00, 0x03, 0x01, 0x00, 0x05};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof(expected));
}

// Length counts the payload plus the two checksum bytes, not the whole frame.
void test_length_field_covers_payload_plus_checksum() {
  uint8_t out[zw111::kMaxFrame];
  const uint8_t args[] = {0x01, 0x00, 0x00, 0x00, 0x64};  // Search buffer 1, page 0..100
  const size_t n = zw111::EncodeCommand(out, sizeof(out), zw111::kDefaultAddress,
                                        Command::kSearch, args, sizeof(args));
  const size_t payload = sizeof(args) + 1;  // instruction byte
  TEST_ASSERT_EQUAL_UINT32(payload + zw111::kFrameOverhead, n);
  const uint16_t declared = static_cast<uint16_t>((out[7] << 8) | out[8]);
  TEST_ASSERT_EQUAL_UINT16(payload + 2, declared);
}

void test_checksum_sums_pid_length_and_payload() {
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  // pid 0x01 + length 0x00 0x05 + payload 1 + 2 + 3
  const uint16_t expected = 0x01 + 0x00 + 0x05 + 0x01 + 0x02 + 0x03;
  TEST_ASSERT_EQUAL_UINT16(expected, zw111::Checksum(PacketId::kCommand, payload, 3));
}

void test_round_trip_through_parser() {
  uint8_t out[zw111::kMaxFrame];
  const uint8_t args[] = {0x02, 0x00, 0x07};
  const size_t n = zw111::EncodeCommand(out, sizeof(out), 0x12345678, Command::kStoreChar,
                                        args, sizeof(args));
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);

  Parser parser;
  TEST_ASSERT_EQUAL(Parser::Result::kFrame, FeedAll(parser, out, n));

  const Frame& frame = parser.frame();
  TEST_ASSERT_EQUAL_UINT32(0x12345678, frame.address);
  TEST_ASSERT_EQUAL(PacketId::kCommand, frame.pid);
  TEST_ASSERT_EQUAL_UINT32(sizeof(args) + 1, frame.length);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Command::kStoreChar), frame.payload[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(args, frame.payload + 1, sizeof(args));
}

// A Search ack carries the confirmation code, then the page id and match score.
void test_parses_search_ack() {
  uint8_t out[zw111::kMaxFrame];
  const uint8_t payload[] = {0x00, 0x00, 0x03, 0x00, 0x8C};
  const size_t n = zw111::Encode(out, sizeof(out), zw111::kDefaultAddress, PacketId::kAck,
                                 payload, sizeof(payload));

  Parser parser;
  TEST_ASSERT_EQUAL(Parser::Result::kFrame, FeedAll(parser, out, n));

  const Frame& frame = parser.frame();
  TEST_ASSERT_TRUE(frame.ok());
  TEST_ASSERT_EQUAL(Status::kOk, frame.status());
  const uint16_t page = static_cast<uint16_t>((frame.payload[1] << 8) | frame.payload[2]);
  const uint16_t score = static_cast<uint16_t>((frame.payload[3] << 8) | frame.payload[4]);
  TEST_ASSERT_EQUAL_UINT16(3, page);
  TEST_ASSERT_EQUAL_UINT16(140, score);
}

void test_ack_with_error_code_is_not_ok() {
  uint8_t out[zw111::kMaxFrame];
  const uint8_t payload[] = {static_cast<uint8_t>(Status::kNotFound)};
  const size_t n = zw111::Encode(out, sizeof(out), zw111::kDefaultAddress, PacketId::kAck,
                                 payload, sizeof(payload));

  Parser parser;
  TEST_ASSERT_EQUAL(Parser::Result::kFrame, FeedAll(parser, out, n));
  TEST_ASSERT_FALSE(parser.frame().ok());
  TEST_ASSERT_EQUAL(Status::kNotFound, parser.frame().status());
}

void test_corrupt_checksum_is_rejected() {
  uint8_t out[zw111::kMaxFrame];
  size_t n = zw111::EncodeCommand(out, sizeof(out), zw111::kDefaultAddress,
                                  Command::kGetImage, nullptr, 0);
  out[n - 1] ^= 0xFF;

  Parser parser;
  TEST_ASSERT_EQUAL(Parser::Result::kBadChecksum, FeedAll(parser, out, n));
}

// The UART may already be mid-transmission when we power up, so the parser has
// to find the next header on its own instead of wedging.
void test_resynchronises_after_garbage() {
  uint8_t frame[zw111::kMaxFrame];
  const size_t n = zw111::EncodeCommand(frame, sizeof(frame), zw111::kDefaultAddress,
                                        Command::kGetImage, nullptr, 0);

  uint8_t stream[zw111::kMaxFrame + 5];
  const uint8_t garbage[] = {0x00, 0xEF, 0xEF, 0x42, 0xEF};
  memcpy(stream, garbage, sizeof(garbage));
  memcpy(stream + sizeof(garbage), frame, n);

  Parser parser;
  TEST_ASSERT_EQUAL(Parser::Result::kFrame, FeedAll(parser, stream, sizeof(garbage) + n));
  TEST_ASSERT_EQUAL(PacketId::kCommand, parser.frame().pid);
}

// A run of 0xEF bytes must not be mistaken for a header; only 0xEF 0x01 is one.
void test_repeated_header_high_byte_does_not_false_start() {
  uint8_t frame[zw111::kMaxFrame];
  const size_t n = zw111::EncodeCommand(frame, sizeof(frame), zw111::kDefaultAddress,
                                        Command::kGetImage, nullptr, 0);

  Parser parser;
  for (int i = 0; i < 8; ++i) {
    TEST_ASSERT_EQUAL(Parser::Result::kNeedMore, parser.Feed(0xEF));
  }
  TEST_ASSERT_EQUAL(Parser::Result::kFrame, FeedAll(parser, frame + 1, n - 1));
}

// An over-long declared length must be refused rather than overrunning the
// buffer, because the length field is attacker-adjacent: it comes off the wire.
void test_oversized_length_is_refused() {
  uint8_t stream[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0xFF, 0xFF};
  Parser parser;
  TEST_ASSERT_EQUAL(Parser::Result::kOverflow, FeedAll(parser, stream, sizeof(stream)));

  // ...and the parser must still work afterwards.
  uint8_t frame[zw111::kMaxFrame];
  const size_t n = zw111::EncodeCommand(frame, sizeof(frame), zw111::kDefaultAddress,
                                        Command::kGetImage, nullptr, 0);
  TEST_ASSERT_EQUAL(Parser::Result::kFrame, FeedAll(parser, frame, n));
}

void test_encode_refuses_a_short_buffer() {
  uint8_t out[4];
  TEST_ASSERT_EQUAL_UINT32(0, zw111::EncodeCommand(out, sizeof(out), zw111::kDefaultAddress,
                                                   Command::kGetImage, nullptr, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_encode_get_image_matches_datasheet);
  RUN_TEST(test_length_field_covers_payload_plus_checksum);
  RUN_TEST(test_checksum_sums_pid_length_and_payload);
  RUN_TEST(test_round_trip_through_parser);
  RUN_TEST(test_parses_search_ack);
  RUN_TEST(test_ack_with_error_code_is_not_ok);
  RUN_TEST(test_corrupt_checksum_is_rejected);
  RUN_TEST(test_resynchronises_after_garbage);
  RUN_TEST(test_repeated_header_high_byte_does_not_false_start);
  RUN_TEST(test_oversized_length_is_refused);
  RUN_TEST(test_encode_refuses_a_short_buffer);
  return UNITY_END();
}
