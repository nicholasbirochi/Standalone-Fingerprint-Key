#include "secret_store.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"

// Present on every ESP-IDF that ships with the Arduino core, but guarded so the
// file still compiles if the header moves.
#if __has_include(<esp_flash_encrypt.h>)
#include <esp_flash_encrypt.h>
#define FPKEY_HAS_FLASH_ENCRYPTION_API 1
#endif

namespace fpkey {
namespace {

constexpr char kNamespace[] = "fpkey";
constexpr char kSecretKey[] = "secret";
constexpr char kTemplateKey[] = "slot";

}  // namespace

void SecretStore::Begin() { Load(); }

void SecretStore::Load() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
    // Namespace does not exist yet: first boot, nothing stored.
    length_ = 0;
    secret_[0] = '\0';
    return;
  }
  const size_t n = prefs.getString(kSecretKey, secret_, sizeof(secret_));
  length_ = n > 0 ? strnlen(secret_, kMaxSecretLength) : 0;
  if (length_ == 0) secret_[0] = '\0';
  allowed_template_ = prefs.getUShort(kTemplateKey, kAnyTemplate);
  prefs.end();
}

bool SecretStore::Read(char* out, size_t capacity) const {
  if (length_ == 0 || capacity <= length_) return false;
  memcpy(out, secret_, length_);
  out[length_] = '\0';
  return true;
}

bool SecretStore::FlashEncrypted() {
#ifdef FPKEY_HAS_FLASH_ENCRYPTION_API
  return esp_flash_encryption_enabled();
#else
  return false;
#endif
}

bool SecretStore::Write(const char* secret) {
  last_error_ = WriteError::kNone;

  const size_t n = secret == nullptr ? 0 : strnlen(secret, kMaxSecretLength + 1);
  if (n > kMaxSecretLength) {
    last_error_ = WriteError::kTooLong;
    return false;
  }
  if (n == 0) {
    Clear();
    return true;
  }

  // Refuse to write a secret the chip would store in the clear. Checked here
  // rather than in the console so that every path into storage is covered.
  if (kRequireFlashEncryption && !FlashEncrypted()) {
    last_error_ = WriteError::kNotEncrypted;
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    last_error_ = WriteError::kStorageFailed;
    return false;
  }
  const bool ok = prefs.putString(kSecretKey, secret) == n;
  prefs.end();
  if (!ok) {
    last_error_ = WriteError::kStorageFailed;
    return false;
  }

  memcpy(secret_, secret, n);
  secret_[n] = '\0';
  length_ = n;
  return true;
}

void SecretStore::Clear() {
  Preferences prefs;
  if (prefs.begin(kNamespace, /*readOnly=*/false)) {
    prefs.remove(kSecretKey);
    prefs.end();
  }
  // Overwrite in RAM as well, so a later crash dump does not carry it.
  memset(secret_, 0, sizeof(secret_));
  length_ = 0;
}

void SecretStore::set_allowed_template(uint16_t page_id) {
  Preferences prefs;
  if (prefs.begin(kNamespace, /*readOnly=*/false)) {
    prefs.putUShort(kTemplateKey, page_id);
    prefs.end();
  }
  allowed_template_ = page_id;
}

}  // namespace fpkey
