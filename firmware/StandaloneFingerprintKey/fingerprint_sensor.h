#pragma once

#include <Arduino.h>

class FingerprintSensor {
 public:
  void begin();
  bool verify();
  uint8_t captureImage();
  bool generateCharacter(uint8_t buffer);
  bool search(uint16_t* id, uint16_t* confidence);
  bool createModel();
  bool storeModel(uint16_t id);
  bool deleteModel(uint16_t id);
  bool templateCount(uint16_t* count);
  uint8_t lastStatus() const { return lastStatus_; }

 private:
  bool command(uint8_t instruction, const uint8_t* args, size_t argCount,
               uint8_t* response, size_t responseCapacity, size_t* responseLength,
               uint32_t timeoutMs = 1000);
  bool readByte(uint8_t* value, uint32_t startedAt, uint32_t timeoutMs);
  bool readAck(uint8_t* response, size_t responseCapacity, size_t* responseLength,
               uint32_t timeoutMs);

  uint8_t lastStatus_ = 0;
};
