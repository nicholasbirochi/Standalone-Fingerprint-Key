# Threat model

Read this before you put a password you care about on one of these.

## What the device actually is

A USB keyboard that holds your password in plaintext and types it when it
recognises a finger.

That sentence is the whole threat model. Everything below is a consequence of
it.

## The requirement this design cannot fully meet

The brief for this project said, plainly: **do not put the real Mac password in
plaintext in the sketch, the repository, flash or logs, and avoid the naive path
of "valid fingerprint => keyboard types a permanently stored password."**

That is the right instinct, and this design does not satisfy it. It is worth
saying why rather than quietly shipping around it.

A USB HID device can only send keystrokes. To get past the macOS login window,
something has to type the password, and the only place a standalone device can
keep it is its own flash. There is no key exchange available: the HLK-ZW111
answers "match" or "no match" and exposes no key material, so the secret cannot
be derived from the finger either. With no software on the host, storing and
typing is the only mechanism there is. The naive path is the only path.

What can be fixed is the *plaintext* half:

- **Enable the chip's flash encryption.** Then the secret is in flash but not
  readable off it, which is the difference between a secret on a device and a
  secret written on the outside of one. `status` reports `at rest` so you always
  know which you have, and setting `kRequireFlashEncryption` in `config.h` makes
  the firmware refuse to store anything until the eFuse is burned.
- **Do not store the password of an account that matters.** A separate,
  unprivileged local account changes the worst case from "my disk is decrypted"
  to "someone can use a throwaway login".
- **Nothing is stored by default.** A freshly flashed device holds no secret and
  types nothing.

None of that makes it a security key. It makes it an honest convenience token.
The rest of this page is what remains true even after you have done all three.

## Where the secret lives

In NVS on the ESP32's SPI flash, unencrypted, unless you enable flash
encryption. Anyone holding the device can read it out:

```sh
esptool.py read_flash 0x9000 0x5000 nvs.bin
strings nvs.bin
```

There is no command in the firmware that prints the secret back over the serial
console -- which is a real mitigation against a curious person with a cable, and
no mitigation at all against anyone willing to spend two minutes with esptool.

**Enable flash encryption** if the device leaves your desk. It is a one-way,
per-device operation and it is documented by Espressif; this repo does not do it
for you because burning eFuses is not something a README should do by surprise.

Once it is on, set `kRequireFlashEncryption = true` in `firmware/include/config.h`
so a future flash of this firmware onto an unprotected chip refuses to store a
secret rather than quietly doing the wrong thing.

Always keep a way in that does not involve this device. macOS will still accept a
typed password, so losing or breaking the key costs you convenience and nothing
else -- but only if you have not made the device the only thing that remembers
the password.

Even with flash encryption, this is a *convenience* token. It is not a
FIDO2 security key, it holds no attested private key, and it cannot prove
anything to anybody.

## The fingerprint is not the secret

Matching happens on the HLK-ZW111. Templates never cross the UART, so firmware
bugs cannot leak a fingerprint. Good.

But the fingerprint only gates *whether the ESP32 types*. It does not encrypt
anything. Desolder the sensor, short the UART lines to an Arduino that replies
with a successful `Search` ack, and the ESP32 types the secret. The sensor is a
button that is hard to press by accident -- not a cryptographic factor.

## What it defends against

- Someone glancing at your keyboard while you log in.
- Typing a long password fifteen times a day, which is how long passwords become
  short passwords.
- A roommate or colleague idly waking your laptop.

## What it does not defend against

- Anyone who takes the device. They have your password.
- Anyone who takes the device *and* your laptop. They have both.
- A machine already compromised. A keyboard is a keyboard.
- Someone replacing the sensor with something that always says yes.

## Consequences you should design around

**Do not use your FileVault or admin password.** Use this for a login password
on a machine where losing that password means "someone can use my laptop", not
"someone can decrypt my disk". Consider a separate, unprivileged account.

**The device types into whatever has focus.** It has no way to know a password
field from a chat window. `kUnlockCooldownMs` limits an accident to a single
occurrence; `kConfirmWithButton = true` in `config.h` requires a deliberate
button press after the match, which eliminates the problem at the cost of one
press per unlock. Turn it on if the key travels.

**Failed attempts are rate-limited, not locked out.** Eight consecutive failures
pause the device for 30 seconds. This slows down someone pressing fingers
against it; it does not stop anyone, and it resets on a power cycle. It is a
brake, not a boundary.

## If you want an actual security key

Use a YubiKey or another FIDO2 authenticator. They hold a private key in a
secure element, they prove possession cryptographically, and they cannot be read
out with a USB-serial adapter. This project is a keyboard that likes your finger,
and it is worth being clear-eyed about the difference.

## Reporting a problem

Open an issue. There is no embargo process for a hobby project, and nothing here
is load-bearing for anybody's infrastructure.
