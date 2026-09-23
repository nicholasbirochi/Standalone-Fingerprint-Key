#include "console.h"

#include <string.h>

#include "config.h"

namespace fpkey {
namespace {

// Enrolment progress is printed from a free function because the sensor's
// callback is a plain function pointer, not a std::function.
void PrintProgress(uint8_t capture, uint8_t of_total, const char* message) {
  Serial.printf("  [%u/%u] %s\n", capture, of_total, message);
}

char* SkipSpaces(char* s) {
  while (*s == ' ' || *s == '\t') ++s;
  return s;
}

// Splits off the first word, returning it and advancing `rest` past it.
char* NextWord(char* s, char** rest) {
  s = SkipSpaces(s);
  char* start = s;
  while (*s != '\0' && *s != ' ' && *s != '\t') ++s;
  if (*s != '\0') {
    *s = '\0';
    *rest = s + 1;
  } else {
    *rest = s;
  }
  return start;
}

bool Equals(const char* a, const char* b) { return strcmp(a, b) == 0; }

bool ParseSlot(const char* text, uint16_t* slot) {
  if (text == nullptr || *text == '\0') return false;

  uint32_t value = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + static_cast<uint8_t>(*p - '0');
    if (value >= kTemplateCapacity) return false;
  }

  *slot = static_cast<uint16_t>(value);
  return true;
}

void PrintSlotRange() {
  Serial.printf("slot must be between 0 and %u\n",
                static_cast<unsigned>(kTemplateCapacity - 1));
}

}  // namespace

void Console::Begin() {
  Serial.begin(115200);
  // No wait loop here: the device has to work when it is plugged into a machine
  // that never opens the port.
}

bool Console::Poll() {
  bool ran = false;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      buffer_[length_] = '\0';
      if (length_ > 0) {
        Dispatch(buffer_);
        ran = true;
      }
      length_ = 0;
      Prompt();
      continue;
    }
    if (c == 8 || c == 127) {  // backspace
      if (length_ > 0) --length_;
      continue;
    }
    if (length_ + 1 < sizeof(buffer_)) {
      buffer_[length_++] = c;
    }
  }
  return ran;
}

void Console::Prompt() { Serial.print("fpkey> "); }

void Console::Dispatch(char* line) {
  char* rest = nullptr;
  char* command = NextWord(line, &rest);

  if (Equals(command, "help") || Equals(command, "?")) {
    PrintHelp();
  } else if (Equals(command, "status")) {
    PrintStatus();
  } else if (Equals(command, "enroll")) {
    RunEnroll(rest);
  } else if (Equals(command, "delete")) {
    char* arg = NextWord(rest, &rest);
    if (*arg == '\0') {
      Serial.println("usage: delete <slot>");
      return;
    }
    uint16_t slot = 0;
    if (!ParseSlot(arg, &slot)) {
      PrintSlotRange();
      return;
    }
    Serial.println(sensor_.DeleteTemplate(slot) ? "deleted" : "delete failed");
  } else if (Equals(command, "clear")) {
    char* arg = NextWord(rest, &rest);
    if (!Equals(arg, "confirm")) {
      Serial.println("this erases every enrolled finger; type: clear confirm");
      return;
    }
    Serial.println(sensor_.ClearLibrary() ? "library cleared" : "clear failed");
  } else if (Equals(command, "secret")) {
    // Everything after the command word is the secret, spaces included.
    char* value = SkipSpaces(rest);
    if (*value == '\0') {
      Serial.println("usage: secret <text>   (or: secret-clear)");
      return;
    }
    if (store_.Write(value)) {
      Serial.printf("stored, %u characters\n", static_cast<unsigned>(store_.length()));
      if (!SecretStore::FlashEncrypted()) {
        Serial.println("WARNING: flash encryption is off, so this is stored in the");
        Serial.println("clear -- anyone holding the board can read it back with");
        Serial.println("esptool. Do not put your Mac login password here.");
        Serial.println("See docs/security.md.");
      }
    } else {
      switch (store_.last_error()) {
        case SecretStore::WriteError::kTooLong:
          Serial.printf("too long; the limit is %u characters\n",
                        static_cast<unsigned>(kMaxSecretLength));
          break;
        case SecretStore::WriteError::kNotEncrypted:
          Serial.println("refused: this build requires flash encryption and this");
          Serial.println("chip does not have it. See docs/security.md.");
          break;
        default:
          Serial.println("could not write to storage");
          break;
      }
    }
    // Scrub the command out of the input buffer so it does not linger in RAM.
    memset(buffer_, 0, sizeof(buffer_));
  } else if (Equals(command, "secret-clear")) {
    store_.Clear();
    Serial.println("secret cleared");
  } else if (Equals(command, "slot")) {
    char* arg = NextWord(rest, &rest);
    if (*arg == '\0') {
      Serial.println("usage: slot <n|any>");
      return;
    }
    uint16_t slot = SecretStore::kAnyTemplate;
    if (!Equals(arg, "any") && !ParseSlot(arg, &slot)) {
      PrintSlotRange();
      return;
    }
    store_.set_allowed_template(slot);
    Serial.println("updated");
  } else if (Equals(command, "test")) {
    char secret[kMaxSecretLength + 1];
    if (!store_.Read(secret, sizeof(secret))) {
      Serial.println("no secret stored");
      return;
    }
    Serial.println("typing in 3s -- focus a text field you can safely type into");
    delay(3000);
    typer_.TypeSecret(secret);
    memset(secret, 0, sizeof(secret));
    Serial.println("done");
  } else if (Equals(command, "lock")) {
    typer_.LockHost();
    Serial.println("sent Control-Command-Q");
  } else if (Equals(command, "reboot")) {
    Serial.println("restarting");
    Serial.flush();
    ESP.restart();
  } else {
    Serial.printf("unknown command: %s (try help)\n", command);
  }
}

void Console::PrintHelp() {
  Serial.println();
  Serial.println("  status            what the device knows right now");
  Serial.println("  enroll [slot]     enrol a finger, default next free slot");
  Serial.println("  delete <slot>     remove one enrolled finger");
  Serial.println("  clear confirm     erase every enrolled finger");
  Serial.println("  secret <text>     set what gets typed after a match");
  Serial.println("  secret-clear      forget it");
  Serial.println("  slot <n|any>      restrict unlocking to one enrolled finger");
  Serial.println("  test              type the secret into the focused window");
  Serial.println("  lock              send the macOS lock shortcut");
  Serial.println("  reboot");
  Serial.println();
  Serial.println("  The secret can never be read back over this port. That is not");
  Serial.println("  the same as it being safe: check 'at rest' in status.");
}

void Console::PrintStatus() {
  const int count = sensor_.TemplateCount();
  Serial.println();
  Serial.printf("  sensor        %s\n", count >= 0 ? "responding" : "not responding");
  if (count >= 0) {
    Serial.printf("  templates     %d of %u\n", count,
                  static_cast<unsigned>(kTemplateCapacity));
  }
  Serial.printf("  secret        %s\n",
                store_.HasSecret() ? "stored (write-only)" : "not set");
  Serial.printf("  at rest       %s\n",
                SecretStore::FlashEncrypted() ? "encrypted (flash encryption on)"
                                              : "PLAINTEXT (flash encryption off)");
  if (store_.allowed_template() == SecretStore::kAnyTemplate) {
    Serial.println("  unlocks with  any enrolled finger");
  } else {
    Serial.printf("  unlocks with  slot %u only\n", store_.allowed_template());
  }
  Serial.printf("  usb hid       %s\n", typer_.Ready() ? "enumerated" : "not enumerated");
  Serial.println();
}

void Console::RunEnroll(char* argument) {
  char* arg = NextWord(argument, &argument);

  int slot = -1;
  if (*arg != '\0') {
    uint16_t parsed = 0;
    if (!ParseSlot(arg, &parsed)) {
      PrintSlotRange();
      return;
    }
    slot = parsed;
  } else {
    const int count = sensor_.TemplateCount();
    if (count < 0) {
      Serial.println("sensor is not responding");
      return;
    }
    slot = count;  // next free slot, assuming a contiguous library
  }
  if (slot < 0 || slot >= kTemplateCapacity) {
    PrintSlotRange();
    return;
  }

  led_.Set(LedState::kEnrolling);
  Serial.printf("enrolling into slot %d, %u captures\n", slot, kEnrollCaptures);

  const bool ok = sensor_.Enroll(static_cast<uint16_t>(slot), kEnrollCaptures, PrintProgress);
  if (ok) {
    Serial.printf("enrolled into slot %d\n", slot);
    led_.Set(LedState::kSuccess);
  } else {
    Serial.printf("enrolment failed: %s\n", zw111::StatusName(sensor_.last_status()));
    led_.Set(LedState::kFailure);
  }
  delay(600);
}

}  // namespace fpkey
