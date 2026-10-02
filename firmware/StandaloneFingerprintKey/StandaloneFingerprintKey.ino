#include <Arduino.h>
#include <string.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

#include "fingerprint_sensor.h"

namespace {

constexpr uint16_t kMaxTemplateId = 49;
constexpr uint8_t kNoFinger = 0x02;
constexpr uint32_t kFingerWaitMs = 20000;
constexpr uint32_t kPollDelayMs = 100;

USBHIDKeyboard keyboard;
FingerprintSensor sensor;
bool sensorReady = false;
bool waitForRemoval = false;
char commandLine[64] = {};
size_t commandLength = 0;

bool waitForImage(const char* prompt) {
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

bool waitForFingerRemoval() {
  Serial.println("[FINGER] retire o dedo");
  const uint32_t startedAt = millis();
  while (millis() - startedAt < kFingerWaitMs) {
    if (sensor.captureImage() == kNoFinger) return true;
    delay(kPollDelayMs);
  }
  Serial.println("[FINGER] tempo esgotado aguardando remocao");
  return false;
}

void sendF13() {
  keyboard.press(KEY_F13);
  delay(40);
  keyboard.releaseAll();
  Serial.println("[HID] F13 enviado");
}

bool parseId(const char* text, uint16_t* id) {
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

void printHelp() {
  Serial.println("Comandos:");
  Serial.println("  help          mostra esta ajuda");
  Serial.println("  status        testa a comunicacao com o sensor");
  Serial.println("  enroll <id>   cadastra uma digital no ID 0..49");
  Serial.println("  delete <id>   apaga o ID 0..49");
  Serial.println("  list          mostra a quantidade de digitais cadastradas");
  Serial.println("  test          envia F13 pelo USB HID");
}

void runStatus() {
  Serial.println("[SENSOR] verificando HLK-ZW111...");
  sensorReady = sensor.verify();
  if (sensorReady) {
    Serial.println("[SENSOR] conectado");
  } else {
    Serial.println("[SENSOR] ERRO: sensor nao respondeu");
    Serial.println("Verifique VCC 3V3, GND, TX->GPIO4, RX->GPIO5 e VT->3V3.");
  }
}

void enroll(uint16_t id) {
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

void runCommand(char* line) {
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

void pollSerial() {
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

void pollFingerprint() {
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

}  // namespace

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
