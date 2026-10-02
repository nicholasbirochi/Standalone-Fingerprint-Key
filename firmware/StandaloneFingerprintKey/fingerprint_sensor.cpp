#include "fingerprint_sensor.h"

#include <string.h>

namespace {

constexpr uint8_t kCommandPacket = 0x01;
constexpr uint8_t kAckPacket = 0x07;
constexpr uint8_t kOk = 0x00;
constexpr size_t kMaxPayload = 64;
constexpr uint8_t kGetImage = 0x01;
constexpr uint8_t kGenerateCharacter = 0x02;
constexpr uint8_t kSearch = 0x04;
constexpr uint8_t kRegisterModel = 0x05;
constexpr uint8_t kStoreCharacter = 0x06;
constexpr uint8_t kDeleteCharacter = 0x0C;
constexpr uint8_t kVerifyPassword = 0x13;
constexpr uint8_t kTemplateCount = 0x1D;

void putU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value);
}

uint16_t getU16(const uint8_t* in) {
  return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

}  // namespace

void FingerprintSensor::begin() {
  Serial1.begin(57600, SERIAL_8N1, 4, 5);
  delay(200);
  while (Serial1.available()) Serial1.read();
}

bool FingerprintSensor::verify() {
  const uint8_t password[] = {0, 0, 0, 0};
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (command(kVerifyPassword, password, sizeof(password), nullptr, 0, nullptr)) return true;
    delay(150);
  }
  return false;
}

uint8_t FingerprintSensor::captureImage() {
  command(kGetImage, nullptr, 0, nullptr, 0, nullptr, 2000);
  return lastStatus_;
}

bool FingerprintSensor::generateCharacter(uint8_t buffer) {
  return command(kGenerateCharacter, &buffer, 1, nullptr, 0, nullptr, 2000);
}

bool FingerprintSensor::search(uint16_t* id, uint16_t* confidence) {
  const uint8_t args[] = {1, 0, 0, 0, 50};
  uint8_t response[4] = {};
  size_t responseLength = 0;
  if (!command(kSearch, args, sizeof(args), response, sizeof(response), &responseLength, 2000)) {
    return false;
  }
  if (responseLength < sizeof(response)) {
    lastStatus_ = 0x01;
    return false;
  }
  *id = getU16(response);
  *confidence = getU16(response + 2);
  return true;
}

bool FingerprintSensor::createModel() {
  return command(kRegisterModel, nullptr, 0, nullptr, 0, nullptr, 2000);
}

bool FingerprintSensor::storeModel(uint16_t id) {
  uint8_t args[] = {1, 0, 0};
  putU16(args + 1, id);
  return command(kStoreCharacter, args, sizeof(args), nullptr, 0, nullptr, 5000);
}

bool FingerprintSensor::deleteModel(uint16_t id) {
  uint8_t args[] = {0, 0, 0, 1};
  putU16(args, id);
  return command(kDeleteCharacter, args, sizeof(args), nullptr, 0, nullptr, 5000);
}

bool FingerprintSensor::templateCount(uint16_t* count) {
  uint8_t response[2] = {};
  size_t responseLength = 0;
  if (!command(kTemplateCount, nullptr, 0, response, sizeof(response), &responseLength) ||
      responseLength < sizeof(response)) {
    return false;
  }
  *count = getU16(response);
  return true;
}

bool FingerprintSensor::command(uint8_t instruction, const uint8_t* args, size_t argCount,
                                uint8_t* response, size_t responseCapacity,
                                size_t* responseLength, uint32_t timeoutMs) {
  uint8_t payload[kMaxPayload] = {};
  const size_t payloadLength = argCount + 1;
  if (payloadLength > sizeof(payload)) {
    lastStatus_ = 0x01;
    return false;
  }
  payload[0] = instruction;
  for (size_t index = 0; index < argCount; ++index) payload[index + 1] = args[index];

  const uint16_t declaredLength = static_cast<uint16_t>(payloadLength + 2);
  uint8_t frame[kMaxPayload + 11] = {};
  frame[0] = 0xEF;
  frame[1] = 0x01;
  frame[2] = 0xFF;
  frame[3] = 0xFF;
  frame[4] = 0xFF;
  frame[5] = 0xFF;
  frame[6] = kCommandPacket;
  frame[7] = static_cast<uint8_t>(declaredLength >> 8);
  frame[8] = static_cast<uint8_t>(declaredLength);
  uint16_t checksum = static_cast<uint16_t>(kCommandPacket + (declaredLength >> 8) +
                                            (declaredLength & 0xFF));
  for (size_t index = 0; index < payloadLength; ++index) {
    frame[9 + index] = payload[index];
    checksum = static_cast<uint16_t>(checksum + payload[index]);
  }
  frame[9 + payloadLength] = static_cast<uint8_t>(checksum >> 8);
  frame[10 + payloadLength] = static_cast<uint8_t>(checksum);

  while (Serial1.available()) Serial1.read();
  Serial1.write(frame, payloadLength + 11);
  Serial1.flush();
  return readAck(response, responseCapacity, responseLength, timeoutMs);
}

bool FingerprintSensor::readByte(uint8_t* value, uint32_t startedAt, uint32_t timeoutMs) {
  while (millis() - startedAt < timeoutMs) {
    if (Serial1.available()) {
      *value = static_cast<uint8_t>(Serial1.read());
      return true;
    }
    delay(1);
  }
  return false;
}

bool FingerprintSensor::readAck(uint8_t* response, size_t responseCapacity,
                                size_t* responseLength, uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  uint8_t value = 0;
  bool headerStarted = false;
  while (millis() - startedAt < timeoutMs) {
    if (!readByte(&value, startedAt, timeoutMs)) break;
    if (!headerStarted) {
      headerStarted = value == 0xEF;
    } else if (value == 0x01) {
      break;
    } else {
      headerStarted = value == 0xEF;
    }
  }
  if (!headerStarted || value != 0x01) {
    lastStatus_ = 0x26;
    return false;
  }

  uint8_t header[7] = {};
  for (uint8_t& byte : header) {
    if (!readByte(&byte, startedAt, timeoutMs)) {
      lastStatus_ = 0x26;
      return false;
    }
  }
  const uint8_t packetId = header[4];
  const uint16_t declaredLength = getU16(header + 5);
  if (packetId != kAckPacket || declaredLength < 3 || declaredLength > kMaxPayload + 2) {
    lastStatus_ = 0x01;
    return false;
  }

  const size_t payloadLength = declaredLength - 2;
  uint8_t payload[kMaxPayload] = {};
  uint16_t checksum = static_cast<uint16_t>(packetId + header[5] + header[6]);
  for (size_t index = 0; index < payloadLength; ++index) {
    if (!readByte(&payload[index], startedAt, timeoutMs)) {
      lastStatus_ = 0x26;
      return false;
    }
    checksum = static_cast<uint16_t>(checksum + payload[index]);
  }
  uint8_t checksumBytes[2] = {};
  for (uint8_t& byte : checksumBytes) {
    if (!readByte(&byte, startedAt, timeoutMs)) {
      lastStatus_ = 0x26;
      return false;
    }
  }
  const uint16_t receivedChecksum = getU16(checksumBytes);
  if (checksum != receivedChecksum) {
    lastStatus_ = 0x01;
    return false;
  }

  lastStatus_ = payload[0];
  const size_t extraLength = payloadLength - 1;
  const size_t copiedLength = min(extraLength, responseCapacity);
  if (response != nullptr && copiedLength > 0) {
    memcpy(response, payload + 1, copiedLength);
  }
  if (responseLength != nullptr) *responseLength = copiedLength;
  return lastStatus_ == kOk;
}
