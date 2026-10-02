#include <Arduino.h>
#include <string.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

// HLK-ZW111 uses EF 01 framing from the ZhianTec/Synochip protocol family.
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

static constexpr uint8_t kCommandPacket = 0x01;
static constexpr uint8_t kAckPacket = 0x07;
static constexpr uint8_t kOk = 0x00;
static constexpr size_t kMaxPayload = 64;
static constexpr uint8_t kGetImage = 0x01;
static constexpr uint8_t kGenerateCharacter = 0x02;
static constexpr uint8_t kSearch = 0x04;
static constexpr uint8_t kRegisterModel = 0x05;
static constexpr uint8_t kStoreCharacter = 0x06;
static constexpr uint8_t kDeleteCharacter = 0x0C;
static constexpr uint8_t kVerifyPassword = 0x13;
static constexpr uint8_t kTemplateCount = 0x1D;
static constexpr uint16_t kMaxTemplateId = 49;
static constexpr uint8_t kNoFinger = 0x02;
static constexpr uint32_t kFingerWaitMs = 20000;
static constexpr uint32_t kPollDelayMs = 100;

static USBHIDKeyboard keyboard;
static FingerprintSensor sensor;
static bool sensorReady = false;
static bool waitForRemoval = false;
static char commandLine[64] = {};
static size_t commandLength = 0;

static void putU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value);
}

static uint16_t getU16(const uint8_t* in) {
  return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8) | in[1]);
}

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
  if (checksum != getU16(checksumBytes)) {
    lastStatus_ = 0x01;
    return false;
  }

  lastStatus_ = payload[0];
  const size_t extraLength = payloadLength - 1;
  const size_t copiedLength = extraLength < responseCapacity ? extraLength : responseCapacity;
  if (response != nullptr && copiedLength > 0) memcpy(response, payload + 1, copiedLength);
  if (responseLength != nullptr) *responseLength = copiedLength;
  return lastStatus_ == kOk;
}

static bool waitForImage(const char* prompt);
static bool waitForFingerRemoval();
static void sendF13();
static bool parseId(const char* text, uint16_t* id);
static void printHelp();
static void runStatus();
static void enroll(uint16_t id);
static void runCommand(char* line);
static void pollSerial();
static void pollFingerprint();

static bool waitForImage(const char* prompt) {
  Serial.println(prompt);
  const uint32_t startedAt = millis();
  uint8_t lastReportedStatus = 0xFF;
  while (millis() - startedAt < kFingerWaitMs) {
    const uint8_t result = sensor.captureImage();
    if (result == 0) {
      Serial.println("[FINGER] imagem capturada");
      return true;
    }
    if (result != kNoFinger && result != lastReportedStatus) {
      Serial.printf("[FINGER] captura falhou (status 0x%02X)\n", result);
      lastReportedStatus = result;
    }
    delay(kPollDelayMs);
  }
  Serial.println("[FINGER] tempo esgotado aguardando dedo");
  return false;
}

static bool waitForFingerRemoval() {
  Serial.println("[FINGER] retire o dedo");
  const uint32_t startedAt = millis();
  while (millis() - startedAt < kFingerWaitMs) {
    if (sensor.captureImage() == kNoFinger) return true;
    delay(kPollDelayMs);
  }
  Serial.println("[FINGER] tempo esgotado aguardando remocao");
  return false;
}

static void sendF13() {
  keyboard.press(KEY_F13);
  delay(40);
  keyboard.releaseAll();
  Serial.println("[HID] F13 enviado");
}

static bool parseId(const char* text, uint16_t* id) {
  if (*text == '\0') return false;
  uint32_t value = 0;
  for (const char* digit = text; *digit != '\0'; ++digit) {
    if (*digit < '0' || *digit > '9') return false;
    value = value * 10 + static_cast<uint8_t>(*digit - '0');
    if (value > kMaxTemplateId) return false;
  }
  *id = static_cast<uint16_t>(value);
  return true;
}

static void printHelp() {
  Serial.println("Comandos:");
  Serial.println("  help          mostra esta ajuda");
  Serial.println("  status        testa a comunicacao com o sensor");
  Serial.println("  enroll <id>   cadastra uma digital no ID 0..49");
  Serial.println("  delete <id>   apaga o ID 0..49");
  Serial.println("  list          mostra a quantidade de digitais cadastradas");
  Serial.println("  test          envia F13 pelo USB HID");
}

static void runStatus() {
  Serial.println("[SENSOR] verificando HLK-ZW111...");
  sensorReady = sensor.verify();
  if (sensorReady) {
    Serial.println("[SENSOR] conectado");
  } else {
    Serial.println("[SENSOR] ERRO: sensor nao respondeu");
    Serial.println("Verifique VCC 3V3, GND, TX->GPIO4, RX->GPIO5 e VT->3V3.");
  }
}

static void enroll(uint16_t id) {
  if (!sensorReady) {
    Serial.println("[SENSOR] execute status e confirme a conexao primeiro");
    return;
  }
  Serial.printf("[ENROLL] cadastro no ID=%u\n", id);
  if (!waitForImage("[ENROLL] 1/6 coloque o dedo")) return;
  Serial.println("[ENROLL] 2/6 leitura realizada");
  if (!sensor.generateCharacter(1)) {
    Serial.printf("[ENROLL] erro ao processar primeira leitura (status 0x%02X)\n",
                  sensor.lastStatus());
    return;
  }
  if (!waitForFingerRemoval()) return;
  if (!waitForImage("[ENROLL] 3/6 coloque novamente o mesmo dedo")) return;
  Serial.println("[ENROLL] 4/6 segunda leitura realizada");
  if (!sensor.generateCharacter(2)) {
    Serial.printf("[ENROLL] erro ao processar segunda leitura (status 0x%02X)\n",
                  sensor.lastStatus());
    return;
  }
  Serial.println("[ENROLL] 5/6 criando modelo");
  if (!sensor.createModel()) {
    Serial.printf("[ENROLL] nao foi possivel criar modelo (status 0x%02X)\n",
                  sensor.lastStatus());
    return;
  }
  Serial.println("[ENROLL] 6/6 salvando no sensor");
  if (!sensor.storeModel(id)) {
    Serial.printf("[ENROLL] nao foi possivel salvar (status 0x%02X)\n", sensor.lastStatus());
    return;
  }
  Serial.printf("[ENROLL] digital salva no ID=%u\n", id);
}

static void runCommand(char* line) {
  char* argument = strchr(line, ' ');
  if (argument != nullptr) {
    *argument++ = '\0';
    while (*argument == ' ') ++argument;
  } else {
    argument = line + strlen(line);
  }

  if (strcmp(line, "help") == 0) {
    printHelp();
  } else if (strcmp(line, "status") == 0) {
    runStatus();
  } else if (strcmp(line, "test") == 0) {
    sendF13();
  } else if (strcmp(line, "enroll") == 0 || strcmp(line, "delete") == 0) {
    uint16_t id = 0;
    if (!parseId(argument, &id)) {
      Serial.println("Uso: enroll <id> ou delete <id> (ID entre 0 e 49)");
      return;
    }
    if (strcmp(line, "enroll") == 0) {
      enroll(id);
    } else if (!sensorReady) {
      Serial.println("[SENSOR] execute status e confirme a conexao primeiro");
    } else if (sensor.deleteModel(id)) {
      Serial.printf("[SENSOR] ID=%u removido\n", id);
    } else {
      Serial.printf("[SENSOR] falha ao remover ID=%u (status 0x%02X)\n", id,
                    sensor.lastStatus());
    }
  } else if (strcmp(line, "list") == 0) {
    uint16_t count = 0;
    if (!sensor.templateCount(&count)) {
      Serial.printf("[SENSOR] nao foi possivel consultar a contagem (status 0x%02X)\n",
                    sensor.lastStatus());
      return;
    }
    Serial.printf("[SENSOR] %u digitais cadastradas\n", count);
    Serial.println("[SENSOR] o protocolo informa a contagem, mas nao enumera os IDs");
  } else {
    Serial.println("Comando desconhecido. Digite help.");
  }
}

static void pollSerial() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      commandLine[commandLength] = '\0';
      if (commandLength > 0) runCommand(commandLine);
      commandLength = 0;
    } else if (commandLength + 1 < sizeof(commandLine)) {
      commandLine[commandLength++] = value;
    }
  }
}

static void pollFingerprint() {
  if (!sensorReady) {
    delay(250);
    return;
  }
  if (waitForRemoval) {
    if (sensor.captureImage() == kNoFinger) {
      Serial.println("[FINGER] dedo removido");
      waitForRemoval = false;
    } else {
      delay(kPollDelayMs);
    }
    return;
  }

  const uint8_t result = sensor.captureImage();
  if (result == kNoFinger) {
    delay(kPollDelayMs);
    return;
  }
  if (result != 0) {
    Serial.printf("[SENSOR] erro de leitura (status 0x%02X); execute status para testar\n",
                  result);
    sensorReady = false;
    return;
  }

  Serial.println("[FINGER] dedo detectado");
  Serial.println("[FINGER] imagem capturada");
  if (!sensor.generateCharacter(1)) {
    Serial.printf("[FINGER] falha ao processar imagem (status 0x%02X)\n", sensor.lastStatus());
    waitForRemoval = true;
    return;
  }
  uint16_t id = 0;
  uint16_t confidence = 0;
  if (sensor.search(&id, &confidence)) {
    Serial.printf("[MATCH] ID=%u confidence=%u\n", id, confidence);
    sendF13();
  } else if (sensor.lastStatus() == 0x09) {
    Serial.println("[MATCH] digital nao cadastrada");
  } else {
    Serial.printf("[MATCH] busca falhou (status 0x%02X)\n", sensor.lastStatus());
  }
  waitForRemoval = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[BOOT] Standalone Fingerprint Key");

  keyboard.begin();
  USB.begin();
  Serial.println("[USB] HID iniciado");

  sensor.begin();
  Serial.println("[UART] RX GPIO4 / TX GPIO5 / 57600 baud / 8N1");
  Serial.println("[SENSOR] procurando HLK-ZW111...");
  runStatus();
  Serial.println("Digite help para ver os comandos.");
}

void loop() {
  pollSerial();
  pollFingerprint();
}
