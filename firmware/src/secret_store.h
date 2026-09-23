// Persistent storage for the secret the device types after a match.
//
// This is NVS, which is *not* encrypted unless the chip's flash encryption is
// enabled. Without it, anyone who can attach a programmer reads the secret
// straight back. FlashEncrypted() reports which of those two worlds you are in,
// and kRequireFlashEncryption in config.h can refuse the unsafe one outright.
// Read docs/security.md before deciding what to put in here.

#pragma once

#include <Arduino.h>

namespace fpkey {

// Long enough for a macOS login password or a passphrase; not for a file.
constexpr size_t kMaxSecretLength = 128;

class SecretStore {
 public:
  void Begin();

  bool HasSecret() const { return length_ > 0; }
  size_t length() const { return length_; }

  // Copies the secret into `out`, NUL-terminated. Returns false if there is no
  // secret or the buffer is too small.
  bool Read(char* out, size_t capacity) const;

  // Writes a new secret and persists it. An empty string clears it. Returns
  // false if the secret is too long, or if kRequireFlashEncryption is set and
  // the chip is storing in plaintext.
  bool Write(const char* secret);

  // Why the last Write() failed, for the console to report.
  enum class WriteError { kNone, kTooLong, kNotEncrypted, kStorageFailed };
  WriteError last_error() const { return last_error_; }

  // True when the chip will encrypt what NVS writes to flash. This is a
  // property of burned eFuses, not of this firmware.
  static bool FlashEncrypted();

  void Clear();

  // Slot in the sensor's library that is allowed to unlock. kAnyTemplate lets
  // every enrolled finger through.
  static constexpr uint16_t kAnyTemplate = 0xFFFF;
  uint16_t allowed_template() const { return allowed_template_; }
  void set_allowed_template(uint16_t page_id);

 private:
  void Load();

  char secret_[kMaxSecretLength + 1] = {};
  size_t length_ = 0;
  uint16_t allowed_template_ = kAnyTemplate;
  WriteError last_error_ = WriteError::kNone;
};

}  // namespace fpkey
